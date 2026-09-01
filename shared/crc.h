// Portable CRC helpers shared by OBC (ESP32), AUX (AVR), ground tools and host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

// CRC-8/SMBUS: poly 0x07, init 0x00, no reflection, no final xor. Used on the OBC<->Nano I2C frames.
static inline uint8_t crc8_smbus(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Bitwise: small code, fine for < 1 KB blobs.
static inline uint32_t crc32_ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *data++;
    for (uint8_t k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF. Reserved for the phase-2 radio frames.
static inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}
