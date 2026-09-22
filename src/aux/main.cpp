/*
 * MYSAT AUX FIRMWARE v2 — Arduino Nano (ATmega328P)
 *
 * Auxiliary controller commanded by the ESP32-CAM OBC over I2C (slave address 0x08):
 *   - servo motor for the solar wings (deploy / retract / arbitrary angle)
 *   - HC-12 radio power pin (D5) and SET pin (D4, AT-command mode)
 *   - status readback (Wire.onRequest) so the OBC finally has telemetry from this board
 *   - AVR watchdog and OBC-heartbeat supervision
 *
 * Protocol: see shared/mysat_icd.h. A single-byte write is a v1.x legacy command; a 4-byte
 * write [0xA5][cmd][arg][crc8] is a v2 command. A 16-byte read returns AuxStatus.
 *
 * Hardware notes (from the stock v1.3.0 firmware):
 *   - Servo power is cut 2.2 s after a move starts because the servo stalls at the end stops.
 *   - RF_ON_PIN low = radio powered (stock firmware drives it low at boot for boards v1.5.5+).
 *   - HC-12 SET pin low = AT mode. We drive it low only while in AT mode and release it
 *     (input, module pull-up) otherwise.
 */
#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>
#include <avr/wdt.h>
#include "mysat_icd.h"
#include "crc.h"

#define AUX_FW_MAJOR 2
#define AUX_FW_MINOR 0

// ---- pins
static const uint8_t MOTOR_PIN  = 9;
static const uint8_t RF_ON_PIN  = 5;
static const uint8_t RF_SET_PIN = 4;

// ---- servo / mechanism
static const uint8_t  CLOSED_ANGLE       = 170;
static const uint8_t  OPENED_ANGLE       = 10;
// These two come from shared/mysat_icd.h so the OBC can pace its commands to what this board
// actually does -- the demonstration show in particular has to leave a full sweep between two
// movements, and that number has to be the same on both sides of the I2C link.
static const uint16_t POWER_SUPPLY_DELAY = AUX_SERVO_POWER_MS;  // ms of servo power per move
static const uint8_t  STEP_DELAY         = AUX_SERVO_STEP_MS;   // ms per degree
static const uint32_t RF_SET_AUTO_EXIT   = 60000; // leave AT mode by itself after 60 s
static const uint32_t HB_STALE_MS        = 60000; // OBC considered silent after 60 s

Servo servo;
static uint8_t  angle = CLOSED_ANGLE;         // last commanded position (servo has no feedback)
static uint8_t  targetAngle = CLOSED_ANGLE;
static uint8_t  servoState = AUX_SERVO_OFF;
static uint32_t powerTimer = 0, stepTimer = 0;

// ---- radio pins
static bool rfSetActive = false;
static uint32_t rfSetSince = 0;
static bool rfPowered = true;

// ---- OBC heartbeat / stats
static bool     hbSeen = false;
static uint32_t hbLastMs = 0;
static uint8_t  cmdCount = 0, crcErrors = 0, bootFlags = 0;
static uint8_t  ledMode = 2;  // 0 off, 1 on, 2 pattern

// ---- I2C receive buffer (filled in ISR, consumed in loop)
static volatile uint8_t rxBuf[8];
static volatile uint8_t rxLen = 0;
static volatile bool    rxPending = false;

static void applyRfPower(bool on) {
  rfPowered = on;
  digitalWrite(RF_ON_PIN, on ? LOW : HIGH);   // active low
}

static void applyRfSet(bool atMode) {
  if (atMode) {
    pinMode(RF_SET_PIN, OUTPUT);
    digitalWrite(RF_SET_PIN, LOW);
    rfSetActive = true;
    rfSetSince = millis();
  } else {
    pinMode(RF_SET_PIN, INPUT);   // release: HC-12 internal pull-up -> transparent mode
    rfSetActive = false;
  }
}

static void requestAngle(uint8_t a) {
  if (a < OPENED_ANGLE) a = OPENED_ANGLE;
  if (a > CLOSED_ANGLE) a = CLOSED_ANGLE;
  targetAngle = a;
  if (servoState == AUX_SERVO_OFF) {
    servo.attach(MOTOR_PIN);
    servo.write(angle);
    powerTimer = millis();
    stepTimer = millis();
    servoState = AUX_SERVO_TURNING;
    Serial.print(F("servo: -> ")); Serial.println(targetAngle);
  }
  // if already turning, the new target simply takes effect on the next step
}

static void handleCommand(uint8_t cmd, uint8_t arg) {
  cmdCount++;
  switch (cmd) {
    case AUX_CMD_NOP: break;
    case AUX_CMD_MOTOR_OPEN:  requestAngle(OPENED_ANGLE); break;
    case AUX_CMD_MOTOR_CLOSE: requestAngle(CLOSED_ANGLE); break;
    case AUX_CMD_SERVO_ANGLE: requestAngle(arg); break;
    case AUX_CMD_RF_SET:      applyRfSet(arg != 0); break;
    case AUX_CMD_RF_POWER:    applyRfPower(arg != 0); break;
    case AUX_CMD_HEARTBEAT:   hbSeen = true; hbLastMs = millis(); break;
    case AUX_CMD_LED:         ledMode = arg > 2 ? 2 : arg; break;
    case AUX_CMD_RESET_STATS: cmdCount = 0; crcErrors = 0; break;
    default: break;
  }
}

static void handleLegacy(uint8_t cmd) {
  switch (cmd) {
    case AUX_LEGACY_MOTOR_OPEN:  handleCommand(AUX_CMD_MOTOR_OPEN, 0); break;
    case AUX_LEGACY_MOTOR_CLOSE: handleCommand(AUX_CMD_MOTOR_CLOSE, 0); break;
    case AUX_LEGACY_RF_TURN:     break;
    case AUX_LEGACY_RF_SET:      handleCommand(AUX_CMD_RF_SET, 1); break;   // auto-exits after 60 s
    default: break;
  }
}

static void onI2cReceive(int n) {
  (void)n;
  uint8_t i = 0;
  while (Wire.available()) {
    uint8_t b = Wire.read();
    if (i < sizeof(rxBuf)) rxBuf[i++] = b;
  }
  rxLen = i;
  rxPending = true;
}

static void onI2cRequest() {
  AuxStatus st;
  st.magic = AUX_STATUS_MAGIC;
  st.proto = AUX_PROTO_VERSION;
  st.fw = (AUX_FW_MAJOR << 4) | (AUX_FW_MINOR & 0x0F);
  st.uptime_s = millis() / 1000UL;
  st.servo_angle = angle;
  st.servo_state = servoState;
  st.rf_flags = (rfSetActive ? 1 : 0) | (rfPowered ? 2 : 0);
  if (!hbSeen) st.hb_age_s = 0xFFFF;
  else {
    uint32_t age = (millis() - hbLastMs) / 1000UL;
    st.hb_age_s = age > 0xFFFE ? 0xFFFE : (uint16_t)age;
  }
  st.cmd_count = cmdCount;
  st.crc_errors = crcErrors;
  st.boot_flags = bootFlags;
  st.crc8 = crc8_smbus((const uint8_t*)&st, AUX_STATUS_LEN - 1);
  Wire.write((const uint8_t*)&st, AUX_STATUS_LEN);
}

static void processRx() {
  if (!rxPending) return;
  uint8_t buf[8]; uint8_t len;
  noInterrupts();
  len = rxLen;
  for (uint8_t i = 0; i < len && i < sizeof buf; i++) buf[i] = rxBuf[i];
  rxPending = false;
  interrupts();

  if (len == 1) { handleLegacy(buf[0]); return; }
  if (len == AUX_FRAME_LEN && buf[0] == AUX_FRAME_MAGIC) {
    if (crc8_smbus(buf, 3) != buf[3]) { crcErrors++; return; }
    handleCommand(buf[1], buf[2]);
  }
}

static void serviceServo() {
  switch (servoState) {
    case AUX_SERVO_TURNING:
      if (millis() - powerTimer >= POWER_SUPPLY_DELAY) {   // protect the stalled servo
        servoState = AUX_SERVO_DONE;
        Serial.println(F("servo: power cutoff"));
        break;
      }
      if (millis() - stepTimer >= STEP_DELAY) {
        stepTimer = millis();
        if (angle < targetAngle) angle++;
        else if (angle > targetAngle) angle--;
        else { servoState = AUX_SERVO_DONE; Serial.println(F("servo: target reached")); break; }
        servo.write(angle);
      }
      break;
    case AUX_SERVO_DONE:
      servo.detach();
      servoState = AUX_SERVO_OFF;
      break;
    default: break;
  }
}

static void serviceLed() {
  // default pattern ----****-*-*---- ; fast blink when the OBC heartbeat went stale
  static const uint16_t normal[] = {400, 200, 100, 200, 100, 3000};
  static const uint16_t alarm[]  = {100, 100, 100, 100, 100, 600};
  static uint8_t step = 0; static uint32_t timer = 0;
  if (ledMode == 0) { digitalWrite(LED_BUILTIN, LOW); return; }
  if (ledMode == 1) { digitalWrite(LED_BUILTIN, HIGH); return; }
  bool stale = hbSeen && (millis() - hbLastMs > HB_STALE_MS);
  const uint16_t* pat = stale ? alarm : normal;
  if (millis() - timer > pat[step]) {
    digitalWrite(LED_BUILTIN, step % 2);
    timer = millis();
    step = (step + 1) % 6;
  }
}

void setup() {
  uint8_t mcusr = MCUSR; MCUSR = 0; wdt_disable();
  if (mcusr & _BV(WDRF)) bootFlags |= 1;
  if (mcusr & _BV(BORF)) bootFlags |= 2;

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RF_ON_PIN, OUTPUT);
  applyRfPower(true);
  applyRfSet(false);

  Wire.begin(MYSAT_AUX_I2C_ADDR);
  Wire.onReceive(onI2cReceive);
  Wire.onRequest(onI2cRequest);

  Serial.begin(115200);
  Serial.print(F("MYSAT AUX v")); Serial.print(AUX_FW_MAJOR); Serial.print('.'); Serial.println(AUX_FW_MINOR);
  Serial.print(F("boot flags: 0x")); Serial.println(bootFlags, HEX);

  wdt_enable(WDTO_8S);
}

void loop() {
  wdt_reset();
  processRx();
  serviceServo();
  if (rfSetActive && millis() - rfSetSince > RF_SET_AUTO_EXIT) applyRfSet(false);
  serviceLed();
}
