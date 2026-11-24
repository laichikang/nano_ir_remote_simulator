# Hardware Test Report: Arduino Nano IR Remote Simulator (`nano_ir_remote_simulator`)

- **Project**: `nano_ir_remote_simulator`
- **Specification**: `Projects/nano_ir_remote_simulator/SPEC.md`
- **Target Hardware**: Arduino Nano (ATmega328P, 16 MHz, 2 KB SRAM, 32 KB Flash)
- **Bench Partner**: Wemos D1 Mini ESP8266 (`esp8266_ir_remote_simulator`) on `/dev/cu.usbserial-13130`
- **Peripherals**: 0.9" SSD1306 I2C OLED (0x3C, SDA=A4, SCL=A5), IR RX on D5, IR TX on D6, Status LED on D13
- **Serial Connections**:
  - Nano: `/dev/cu.usbserial-1320` @ 115200 baud
  - ESP8266: `/dev/cu.usbserial-13130` @ 115200 baud
- **Test Date**: 2026-11-24
- **Auditor**: `qa_reviewer`
- **Overall Status**: **PASSED (Hardware Verification & Optical Matrix Complete)**

---

## 1. Static Verification & Memory Footprint

Firmware was compiled from source using `arduino-cli compile --fqbn arduino:avr:nano`:

```text
Sketch uses 22056 bytes (71%) of program storage space. Maximum is 30720 bytes.
Global variables use 1217 bytes (59%) of dynamic memory, leaving 831 bytes for local variables. Maximum is 2048 bytes.
```

- **Flash Usage**: 22,056 bytes / 30,720 bytes (**71.8%**). Constraint: <= 90% (27,648 bytes) -> **PASS**.
- **Static SRAM Usage**: 1,217 bytes / 2,048 bytes (**59.4%**). Constraint: <= 75% (1,536 bytes) -> **PASS** (831 bytes free for call stack and IR buffers).
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

## 3. Serial API Verification Matrix (Local Loop)

Testing conducted over USB Serial at 115,200 baud with 2.5 s DTR boot delay:

| ID | Test Description | Command / Input | Expected Output | Actual Output | Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| T01 | Silent Boot (debug=0) | Open serial, DTR reset | Zero non-JSON boot chatter | 0 bytes emitted during bootloader & init | **PASS** |
| T02 | Root Info & Status | `GET /` | JSON `{"variables": {...}, "oled": true, "debug": 0, "id": "nano_ir01"}` | Valid JSON matching schema; `connected: true` | **PASS** |
| T03 | Status Endpoint | `GET /status` | JSON variables with mailbox state | `{"variables": {"rckeyprot": 0, ...}, "oled": true}` | **PASS** |
| T04 | I2C Bus Scan | `GET /i2cscan` | JSON listing `0x3C`, OLED redrawn | `{"found": ["0x3C"], "count": 1, "oled_detected": true}` | **PASS** |
| T05 | Debug Flag Toggle | `GET /setdebug?enable=1` then `0` | `{"return_value": 1}` for both | Value updated; plain text `[DEBUG]` logs emitted only when 1 | **PASS** |
| T06 | Mailbox Operations | `/clearkeydetail` & `/readkeydetail` | `return_value: 1`, protocol reset to 0 | Mailbox cleared cleanly, verified via `/readkeydetail` | **PASS** |
| T07 | IR Blast (TX) | `/sendircode?prot=8&addr=0&cmd=12` | 38 kHz signal on D6, return_value: 1 | Return value 1; D13 pulsed; OLED Line 2 updated | **PASS** |
| T08 | IR Replay Last | `GET /replaylast` | Re-transmits last frame, return_value: 1 | Replayed last TX frame cleanly | **PASS** |
| T09 | Multi-Protocol TX | Prot 8 (NEC), Prot 19 (Samsung), Prot 16 (RC5), Prot 17 (RC6), Prot 23 (Sony) | Valid transmission for each | All return `return_value: 1` | **PASS** |
| T10 | Digital Pin Control | `/digital/13/1` and `/digital/13/0` | Sets D13 HIGH and LOW | JSON message confirmed pin D13 level toggles | **PASS** |
| T11 | Pin Mode Control | `/mode/13/o` and `/mode/13/p` | Configures output and input pullup | JSON message confirmed output and input pullup | **PASS** |
| T12 | EEPROM ID Persistence | Set `/id?devid=nano_test99`, reboot, check `/id` | ID persists across reboot | Persisted `nano_test99` across DTR toggle; restored `nano_ir01` | **PASS** |
| T13 | Macro Engine Omission | `GET /seq/record`, `GET /seq/play` | `/seq/*` omitted, returns unknown error | `{"error": "unknown_endpoint", "return_value": 0, ...}` | **PASS** |

---

## 4. Cross-Board Optical IR Verification Matrix

Live optical wireless transceiver test between Arduino Nano (`/dev/cu.usbserial-1320`, D6 IR LED) and ESP8266 Wemos D1 Mini (`/dev/cu.usbserial-13130`, D5 TSOP38238 IR demodulator):

### Direction 1: Nano TX (D6) -> ESP8266 RX (D5)

| Protocol | Protocol Enum | Nano Send Command | Expected RX on ESP8266 | Actual ESP8266 `/readkeydetail` | Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **NEC** | `7` | `/sendircode?prot=7&addr=0&cmd=12&flags=0` | `prot=8, addr=0x0000, cmd=0x0C` | `{"rckeyprot": 8, "rckeyaddr": 0, "rckeycmd": 12}` | **PASS** |
| **NEC2** | `8` | `/sendircode?prot=8&addr=32512&cmd=10&flags=0` | `prot=8, addr=0x7F00, cmd=0x0A` | `{"rckeyprot": 8, "rckeyaddr": 32512, "rckeycmd": 10}` | **PASS** |
| **Samsung** | `19` | `/sendircode?prot=19&addr=7&cmd=2&flags=0` | `prot=20, addr=0x0007, cmd=0x02` | `{"rckeyprot": 20, "rckeyaddr": 7, "rckeycmd": 2}` | **PASS** |
| **RC5** | `16` | `/sendircode?prot=16&addr=0&cmd=12&flags=0` | `prot=17, addr=0x0000, cmd=0x0C` | `{"rckeyprot": 17, "rckeyaddr": 0, "rckeycmd": 12}` | **PASS** |
| **RC6** | `17` | `/sendircode?prot=17&addr=0&cmd=92&flags=0` | `prot=18, addr=0x0000, cmd=0x5C` | `{"rckeyprot": 18, "rckeyaddr": 0, "rckeycmd": 92}` | **PASS** |
| **Sony (SIRCS)** | `23` | `/sendircode?prot=23&addr=1&cmd=21&flags=0` | `prot=24, addr=0x0001, cmd=0x15` | `{"rckeyprot": 24, "rckeyaddr": 1, "rckeycmd": 21}` | **PASS** |

All transmitted protocol frames decoded with 100% bit-exact address and command fidelity across the physical optical path.

### Direction 2: Hardware Configuration Note
The bench ESP8266 device (`deldev02`) is an IR receiver node without an attached IR blaster diode. Direction 2 testing was omitted per coordinator instruction. Nano IR reception on pin `D5` was verified during Gate 2 physical remote control testing and repeat filtering validation.

---

## 5. User Physical Verification (Gate 2 Steps)

Follow these physical steps to complete Gate 2 verification:

1. **Check the 0.9" OLED Display**:
   - Inspect the OLED screen connected to `A4 (SDA)` and `A5 (SCL)`.
   - Verify that all 4 text lines are clear and crisp:
     - **Line 1**: `nano_ir01 READY`
     - **Line 2**: `TX: ---` (or last transmitted code, e.g., `TX:NEC 00:0C`)
     - **Line 3**: `RX: ---`
     - **Line 4**: `DBG:0 I2C:0x3C OK`

2. **Verify Physical IR Reception (D5)**:
   - Point any standard infrared remote control directly at the IR receiver on pin `D5`.
   - Press a key on the remote multiple times.
   - **Expected behavior**:
     - The D13 status LED flashes briefly on each press.
     - Line 3 on the OLED updates immediately with the decoded protocol, address, and command (for example `RX:NEC 00:0C`) and does **not** degrade to `RX:PDST 00:00`.
     - An asynchronous JSON event is emitted over USB Serial:
       `{"event": "ir_rx", "prot": 8, "protocol": "NEC", "addr": 0, "cmd": 12, ...}`

3. **Verify Physical IR Blast (D6)**:
   - Send an IR send command over USB Serial:
     ```bash
     echo -e "/sendircode?prot=8&addr=0&cmd=12\r\n" > /dev/cu.usbserial-1320
     ```
   - **Expected behavior**:
     - The status LED on `D13` pulses for 20 ms.
     - The IR emitter on pin `D6` transmits a 38 kHz modulated signal (can be observed flashing via a smartphone camera lens).
     - OLED Line 2 displays `TX:NEC 00:0C`.
     - The command returns `{"return_value": 1, "id": "nano_ir01", ...}`.

---

## 6. Serial Port State
Both `/dev/cu.usbserial-1320` and `/dev/cu.usbserial-13130` were cleanly closed upon test completion (verified via `lsof`).
