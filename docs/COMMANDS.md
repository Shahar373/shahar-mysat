# Command reference

One interpreter (`src/obc/apps/console.cpp`) serves three entry points:
- the serial console (also whatever's on the far end of the HC-12, since it shares the UART),
- `POST /api/cmd` with the command line as the request body (or `?cmd=`),
- the web dashboard's Control tab and Console tab, which just call the same endpoint.

Every command is one line, arguments separated by spaces; wrap an argument in `"double quotes"` if
it needs to contain a space (e.g. a WiFi SSID). Unknown commands print `? unrecognized, try 'help'`
rather than being silently ignored.

## Status & help

| Command | Effect |
|---|---|
| `help` / `?` | prints this list |
| `status` | one telemetry frame immediately, regardless of the console mode |

## Solar panels (via AUX)

| Command | Effect |
|---|---|
| `solar deploy` | drive the wings to the open angle |
| `solar retract` | drive the wings to the closed angle |
| `solar toggle` | flip whichever state they're currently commanded to |
| `solar angle <0-180>` | arbitrary servo angle — the mechanism holds intermediate positions, so this is the hook for a future sun-tracking array drive |

## LEDs

| Command | Effect |
|---|---|
| `led toggle` | STAR LED on/off |
| `led blink` | 3-blink test pattern, then restores the previous state |

The SIGNAL LED (NeoPixel) is not manually commandable — it always shows system status: white
breathing at boot, green heartbeat once nominal, blue slow-blink while a WiFi station connection is
down, amber solid while serving its own access point, red double-blink if any sensor has been
isolated by FDIR.

## Attitude

| Command | Effect |
|---|---|
| `imu calibrate` | 3 s gyro-bias calibration — hold the satellite still |
| `imu align` | zero roll/pitch/yaw at the current attitude, without a full recalibration |

## Console / telemetry output

| Command | Effect |
|---|---|
| `console text` | human-readable telemetry frame every `tm.period_ms` |
| `console plotter <0-3>` | Arduino Serial Plotter-friendly CSV: 0=environment, 1=attitude, 2=sun sensors, 3=power |
| `console toggle` | mute/unmute the periodic frame entirely (commands still work) |
| `loglevel <0-3>` | serial verbosity: 0 error, 1 warn, 2 info (default), 3 debug |

## Mission data logging

| Command | Effect |
|---|---|
| `log start <period_s>` | begin CSV logging at the given period; rotates hourly, caps at 20000 rows total (oldest file deleted first) |
| `log stop` | stop logging (buffered rows are flushed first) |
| `log delete` | stop and delete every log file |
| `log list` | file names and sizes |

## Identity, clock, network

| Command | Effect |
|---|---|
| `callsign <name>` | sets the on-air identity (used in the AP SSID and telemetry frames) |
| `time set <YYYY-MM-DDTHH:MM:SS>` | sets the system clock (UTC). No RTC hardware present yet? this still works, it just won't survive a power-off. |
| `wifi mode off\|sta\|ap` | selects the WiFi behaviour; `sta` tries the configured network for `wifi.sta_timeout_s` seconds, then falls back to its own access point automatically |
| `wifi ssid <name>` / `wifi pass <pass>` | configure the station network to try |
| `wifi apply` | reconnect now using the current settings (no reboot needed) |

Default out of the box: **access point mode**, SSID `<callsign>-XXXX`, open (no password) unless
`ap.pass` is set. This is deliberate — the satellite is meant to work standing on a table with
nothing else, per how you said you'll mostly use it.

## Radio (HC-12, via AUX)

| Command | Effect |
|---|---|
| `radio at [on\|off]` | pulls the HC-12 SET pin (AT-command mode) so you can reconfigure the module; auto-exits after 60 s if you forget |
| `radio power on\|off` | cuts/restores power to the HC-12 |

## Photos (secondary payload)

| Command | Effect |
|---|---|
| `photo` | capture now, save to the on-board ring (8 SVGA frames) |
| `photo list` | JSON index of what's stored |

The web dashboard's Photos tab shows a gallery and lets you capture from there too
(`GET /api/photo/capture`).

## Events & diagnostics

| Command | Effect |
|---|---|
| `events` | dump the on-board event log (boot/shutdown, sensor faults and recoveries, commands, photos, params changes, ...) |
| `events clear` | erase it |
| `fs` | LittleFS space used/total |
| `params list` | every parameter, current value (secrets shown as `***`) |
| `params get <key>` | one value, in full (secrets included) |
| `params set <key> <value>` | change and save one parameter; rejected if out of range |
| `params save` | force-write the working copy (usually not needed, most commands save automatically) |
| `reboot` | restart now |

## Commands from the stock firmware (still work)

Old habits and old muscle memory aren't broken: a single legacy word is rewritten to its modern
equivalent before dispatch (see `LEGACY_ALIASES` in `shared/cmdline.h`).

| Old command | Now runs |
|---|---|
| `SolarDeploy` | `solar deploy` |
| `SolarRetract` | `solar retract` |
| `SolarMove` | `solar toggle` |
| `TurnLed` | `led toggle` |
| `BlinkLed` | `led blink` |
| `Calibrate` | `imu calibrate` |
| `TurnConsole` | `console toggle` |
| `SwitchTelemetry` | `console plotter` |
| `DebugModeOn` / `DebugModeOff` | `loglevel 3` / `loglevel 2` |
| `StartLogging` / `StopLogging` / `DeleteLogging` / `ListLogFiles` | `log start` / `log stop` / `log delete` / `log list` |
| `AuditFileSystem` | `fs` |
| `SendEventLog` | `events` |
| `SetRadio` | `radio at` |

Two stock commands don't have a direct equivalent because the interaction model changed:
`SetWIFI` and `SetCallSign` used to prompt you for input; use `wifi mode/ssid/pass` and `callsign
<name>` instead. `ChangeTime`'s interactive prompt is now `time set <ISO8601>`.
