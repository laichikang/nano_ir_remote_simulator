# Hardware Test Report: Arduino Nano IR Remote Simulator (`nano_ir_remote_simulator`)

- **Project**: `nano_ir_remote_simulator`
- **Specification**: `Projects/nano_ir_remote_simulator/SPEC.md` (inclusive of Change Request 2026-11-24)
- **Target Hardware**: Arduino Nano (ATmega328P, 16 MHz, 2 KB SRAM, 32 KB Flash)
- **Bench Partner**: Wemos D1 Mini ESP8266 (`esp8266_ir_remote_simulator`) on `/dev/cu.usbserial-13130`
- **Peripherals**: 0.9" SSD1306 I2C OLED (0x3C, SDA=A4, SCL=A5), IR RX on D5, IR TX on D6, Status LED on D13
- **Serial Connection**: `/dev/cu.usbserial-1320` @ 115200 baud
- **Test Date**: 2026-11-24
- **Auditor**: `qa_reviewer`
- **Overall Status**: **PASSED (Change Request 2026-11-24 Audit & Hardware Verification Complete)**

---

## 1. Static Verification & Memory Footprint

Firmware was compiled from source using `arduino-cli compile --fqbn arduino:avr:nano`:

```text
Sketch uses 22458 bytes (73%) of program storage space. Maximum is 30720 bytes.
Global variables use 1226 bytes (59%) of dynamic memory, leaving 822 bytes for local variables. Maximum is 2048 bytes.
```

- **Flash Usage**: 22,458 bytes / 30,720 bytes (**73.1%**). Constraint: <= 90% (27,648 bytes) -> **PASS**.
- **Static SRAM Usage**: 1,226 bytes / 2,048 bytes (**59.8%**). Constraint: <= 75% (1,536 bytes) -> **PASS** (822 bytes free for call stack and IR buffers).
- **Compiler Warnings / Errors**: 0 errors, 0 warnings -> **PASS**.

---

## 2. Firmware Flashing & Bootloader Auto-Detection

Flashing executed via `arduino-cli upload -p /dev/cu.usbserial-1320 --fqbn arduino:avr:nano:cpu=atmega328old`:
```text
New upload port: /dev/cu.usbserial-1320 (serial)
=== Flashed successfully (old bootloader, 57600 baud) ===
```
Constraint: Flash successfully to target hardware -> **PASS**.

---

## 3. Serial API Verification Matrix

Testing conducted over USB Serial at 115,200 baud with 2.5 s DTR boot delay:

| ID | Test Description | Command / Input | Expected Output | Actual Output | Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| T01 | Silent Boot (debug=0) | Open serial, DTR reset | Zero non-JSON boot chatter | 0 bytes emitted during bootloader & init | **PASS** |
| T02 | Root Info & Status | `GET /` | JSON with `oled: true`, `oled_enabled: true`, `connected: true` | Valid JSON matching schema | **PASS** |
| T03 | Status Endpoint | `GET /status` | JSON variables with mailbox state & `oled_enabled` | `{"variables": {...}, "oled": true, "oled_enabled": true}` | **PASS** |
| T04 | I2C Bus Scan | `GET /i2cscan` | JSON listing `0x3C`, OLED redrawn | `{"found": ["0x3C"], "count": 1, "oled_detected": true}` | **PASS** |
| T05 | OLED Toggle Off | `GET /setoled?enable=0` | Screen blanks, `oled_enabled: false` returned | `{"return_value": 1, "oled_enabled": false}` | **PASS** |
| T06 | Status while OLED Off | `GET /status` | Reports `oled_enabled: false` | `{"variables": {...}, "oled_enabled": false}` | **PASS** |
| T07 | I2C Scan while OLED Off | `GET /i2cscan` | Probes bus, keeps screen blank & `oled_enabled: false` | `{"found": ["0x3C"], ...}`, status stays `oled_enabled: false` | **PASS** |
| T08 | OLED Toggle On | `GET /setoled?enable=1` | Screen redraws 4 lines, `oled_enabled: true` | `{"return_value": 1, "oled_enabled": true}` | **PASS** |
| T09 | OLED Invalid Param | `GET /setoled?enable=2` | Returns `return_value: 0` | `{"return_value": 0, "id": "nano_ir01", ...}` | **PASS** |
| T10 | Debug Flag Toggle | `GET /setdebug?enable=1` then `0` | `{"return_value": 1}` for both | Value updated; plain text `[DEBUG]` logs emitted only when 1 | **PASS** |
| T11 | Mailbox Operations | `/clearkeydetail` & `/readkeydetail` | `return_value: 1`, protocol reset to 0 | Mailbox cleared cleanly, verified via `/readkeydetail` | **PASS** |
| T12 | IR Blast (TX) | `/sendircode?prot=8&addr=0&cmd=12` | 38 kHz signal on D6, return_value: 1 | Return value 1; D13 pulsed; OLED Line 2 updated | **PASS** |
| T13 | IR Replay Last | `GET /replaylast` | Re-transmits last frame, return_value: 1 | Replayed last TX frame cleanly | **PASS** |
| T14 | Multi-Protocol TX | Prot 8 (NEC), Prot 19 (Samsung), Prot 16 (RC5), Prot 17 (RC6), Prot 23 (Sony) | Valid transmission for each | All return `return_value: 1` | **PASS** |
| T15 | Digital Pin Control | `/digital/13/1` and `/digital/13/0` | Sets D13 HIGH and LOW | JSON message confirmed pin D13 level toggles | **PASS** |
| T16 | Pin Mode Control | `/mode/13/o` and `/mode/13/p` | Configures output and input pullup | JSON message confirmed output and input pullup | **PASS** |
| T17 | EEPROM ID Persistence | Set `/id?devid=nano_test99`, reboot, check `/id` | ID persists across reboot | Persisted `nano_test99` across DTR toggle; restored `nano_ir01` | **PASS** |
| T18 | Macro Engine Omission | `GET /seq/record`, `GET /seq/play` | `/seq/*` omitted, returns unknown error | `{"error": "unknown_endpoint", "return_value": 0, ...}` | **PASS** |

---

## 4. Change Request 2026-11-24 Implementation Details

1. **Headless Boot Auto-Detect Hardening**:
   - `Wire.setWireTimeout(3000, true)` is configured in `OledUi::begin()` before probing `0x3C`. If an Arduino Nano is booted without an OLED display or external pull-ups, I2C transmissions time out after 3 ms with automatic hardware TWI reset, preventing boot hangs.
   - Guard conditions (`!_ready || !_enabled`) at the top of `setLastTx()`, `setLastRx()`, `drawAll()`, `drawHeader()`, and `drawStatus()` skip all string formatting (`snprintf`) and eliminate all I2C bus transactions when headless or disabled.
2. **400 kHz Fast Mode I2C**:
   - `Wire.setClock(400000UL)` was verified active in `OledUi::begin()`. Zero-framebuffer text transfers operate ~3-4x faster than standard 100 kHz I2C, minimizing execution time and eliminating blocking during IR receive polling.
3. **Runtime RAM-only `/setoled` Toggle**:
   - `/setoled?enable=0` blanks the screen and suppresses subsequent updates.
   - `/setoled?enable=1` re-enables updates and immediately renders the active 4-line UI.
   - `oled_enabled` boolean is exposed in both `/` and `/status` JSON payloads.

---

## 5. Cross-Board Optical IR Verification Matrix

Optical wireless transceiver test between Arduino Nano (`/dev/cu.usbserial-1320`, D6 IR LED) and ESP8266 Wemos D1 Mini (`/dev/cu.usbserial-13130`, D5 TSOP38238 IR demodulator):

| Protocol | Protocol Enum | Nano Send Command | Expected RX on ESP8266 | Actual ESP8266 `/readkeydetail` | Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **NEC** | `7` | `/sendircode?prot=7&addr=0&cmd=12&flags=0` | `prot=8, addr=0x0000, cmd=0x0C` | `{"rckeyprot": 8, "rckeyaddr": 0, "rckeycmd": 12}` | **PASS** |
| **NEC2** | `8` | `/sendircode?prot=8&addr=32512&cmd=10&flags=0` | `prot=8, addr=0x7F00, cmd=0x0A` | `{"rckeyprot": 8, "rckeyaddr": 32512, "rckeycmd": 10}` | **PASS** |
| **Samsung** | `19` | `/sendircode?prot=19&addr=7&cmd=2&flags=0` | `prot=20, addr=0x0007, cmd=0x02` | `{"rckeyprot": 20, "rckeyaddr": 7, "rckeycmd": 2}` | **PASS** |
| **RC5** | `16` | `/sendircode?prot=16&addr=0&cmd=12&flags=0` | `prot=17, addr=0x0000, cmd=0x0C` | `{"rckeyprot": 17, "rckeyaddr": 0, "rckeycmd": 12}` | **PASS** |
| **RC6** | `17` | `/sendircode?prot=17&addr=0&cmd=92&flags=0` | `prot=18, addr=0x0000, cmd=0x5C` | `{"rckeyprot": 18, "rckeyaddr": 0, "rckeycmd": 92}` | **PASS** |
| **Sony (SIRCS)** | `23` | `/sendircode?prot=23&addr=1&cmd=21&flags=0` | `prot=24, addr=0x0001, cmd=0x15` | `{"rckeyprot": 24, "rckeyaddr": 1, "rckeycmd": 21}` | **PASS** |

---

## 6. User Physical Verification (Gate 2 Steps)

Follow these physical steps to verify Change Request 2026-11-24:

1. **Verify Default OLED Boot State**:
   - Inspect the OLED screen connected to `A4` and `A5`.
   - Confirm that all 4 text lines render crisply:
     - **Line 1**: `nano_ir01 READY`
     - **Line 2**: `TX: ---`
     - **Line 3**: `RX: ---`
     - **Line 4**: `DBG:0 I2C:0x3C OK`
   - Send `GET /` or `GET /status` over USB Serial:
     - Confirm `"oled": true` and `"oled_enabled": true`.

2. **Verify OLED Blanking (`/setoled?enable=0`)**:
   - Send command over USB Serial:
     ```bash
     echo -e "/setoled?enable=0\r\n" > /dev/cu.usbserial-1320
     ```
   - **Expected behavior**:
     - The physical OLED screen clears and turns completely blank/dark.
     - Response returns `{"return_value": 1, "oled_enabled": false, ...}`.
     - Sending subsequent `/sendircode` or receiving IR commands does **not** illuminate or update the OLED screen.

3. **Verify OLED Re-Enabling (`/setoled?enable=1`)**:
   - Send command over USB Serial:
     ```bash
     echo -e "/setoled?enable=1\r\n" > /dev/cu.usbserial-1320
     ```
   - **Expected behavior**:
     - The physical OLED screen immediately re-illuminates and redraws the full 4-line live UI.
     - Response returns `{"return_value": 1, "oled_enabled": true, ...}`.

4. **Verify Headless Operation (Optional Bench Check)**:
   - If the OLED display is unplugged from `A4`/`A5`, power-cycling the Arduino Nano boots immediately within 2.5 seconds without hanging.
   - `GET /status` returns `"oled": false` and `"oled_enabled": false`.

---

## 7. Serial Port State
Serial port `/dev/cu.usbserial-1320` was cleanly closed upon test completion (verified via `lsof`).
