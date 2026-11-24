# Arduino Nano IR Remote Simulator (`nano_ir_remote_simulator`)

USB Serial infrared transceiver and live OLED status firmware for the classic **Arduino Nano (ATmega328P, 16 MHz, 2 KB SRAM / 32 KB Flash)**.

Forked from `pico_ir_remote_simulator` and optimized for ATmega328P memory constraints:
- **Zero-framebuffer OLED UI (400 kHz I2C)**: Uses `SSD1306Ascii` over hardware Fast Mode I2C (`0x3C`, 400 kHz) instead of `Adafruit_SSD1306`, saving 1 KB of SRAM and minimizing redraw latency.
- **Headless Boot Auto-Detect**: Sets `Wire.setWireTimeout(3000, true)` before probing `0x3C` so a Nano without an OLED or external I2C pull-ups boots cleanly and skips all OLED I2C traffic.
- **No `/seq/*` macro engine**: Drops the 128-slot RAM buffer to keep static SRAM comfortably below 75% of 2048 bytes.
- **Silent JSON by default (`debug = 0`, ADR-005)**: Emits pure JSON over USB Serial at 115200 baud with zero boot chatter.

---

## 1. Hardware Wiring & Pinout

| Component | Arduino Nano Pin | Notes |
| :--- | :--- | :--- |
| **IR Receiver (RX)** | `D5` | Active-LOW 38 kHz IR demodulator (e.g., TSOP38238 / VS1838B); power with 5V |
| **IR Blaster (TX)** | `D6` | 38 kHz carrier via `IRremote` v4.x software PWM (`IR_SEND_PIN = 6`) |
| **0.9" SSD1306 OLED** | `A4` (SDA), `A5` (SCL) | Hardware I2C (`0x3C`), 5V/3.3V per module spec |
| **Status LED** | `D13` (`LED_BUILTIN`) | Flashes on IR TX/RX activity; controllable via `/digital/13/<0|1>` |
| **EEPROM** | Internal 1 KB | Persists device ID (default: `nano_ir01`) |

### 4-Line Live OLED UI
- **Line 1**: Device ID and ready state (e.g., `nano_ir01 READY`)
- **Line 2**: Last transmitted IR frame (e.g., `TX:NEC 00:0C`)
- **Line 3**: Last received IR frame (e.g., `RX:NEC 00:0C`)
- **Line 4**: Debug flag and I2C status (e.g., `DBG:0 I2C:0x3C OK`)

---

## 2. Build & Flash Instructions

### Prerequisites
Install `arduino-cli`, the AVR core, and required libraries:

```bash
arduino-cli core install arduino:avr
arduino-cli lib install IRremote SSD1306Ascii
```

### Automated Compile & Flash (Auto-Detects New vs Old Bootloader)

```bash
chmod +x scripts/build_and_flash.sh

# Compile only
./scripts/build_and_flash.sh compile

# Compile and flash (auto-detects USB serial port and new vs old bootloader)
./scripts/build_and_flash.sh all

# Flash to a specific port
./scripts/build_and_flash.sh flash /dev/cu.usbserial-1320
```

### Manual `arduino-cli` Commands

```bash
# Compile
arduino-cli compile --fqbn arduino:avr:nano nano_ir_remote_simulator

# Flash (New Bootloader - 115200 baud)
arduino-cli upload -p /dev/cu.usbserial-1320 --fqbn arduino:avr:nano nano_ir_remote_simulator

# Flash (Old Bootloader - 57600 baud)
arduino-cli upload -p /dev/cu.usbserial-1320 --fqbn arduino:avr:nano:cpu=atmega328old nano_ir_remote_simulator
```

---

## 3. Serial API Reference (115200 baud, `\r\n` terminated)

| Endpoint | Description | Example Response |
| :--- | :--- | :--- |
| `/` or `/status` | Status, OLED state (`oled`, `oled_enabled`), and IR capture mailbox | `{"variables": {"rckeyprot": 0, ...}, "debug": 0, "oled": true, "oled_enabled": true, "id": "nano_ir01", ...}` |
| `/readkeydetail` | Read captured IR mailbox (alias of `/status`) | Same as `/status` |
| `/clearkeydetail` | Clear pending IR mailbox to accept new capture | `{"return_value": 1, "id": "nano_ir01", ...}` |
| `/sendircode?prot=<p>&addr=<a>&cmd=<c>&flags=<f>` | Transmit 38 kHz IR frame on D6 and update OLED Line 2 | `{"return_value": 1, "id": "nano_ir01", ...}` |
| `/replaylast` | Re-transmit last RX frame (or last TX frame) | `{"return_value": 1, "id": "nano_ir01", ...}` |
| `/id` | Get persisted device ID | `{"id": "nano_ir01", "name": "nano_ir", "hardware": "arduino", "connected": true}` |
| `/id?devid=<id>` (or `/setupconfig?devid=<id>`) | Set and persist device ID in EEPROM (max 20 chars) | `{"return_value": 1, "id": "<id>", ...}` |
| `/setdebug?enable=<0\|1>` | Toggle plain-text `[DEBUG]` logs (default `0`) | `{"return_value": 1, "id": "nano_ir01", ...}` |
| `/setoled?enable=<0\|1>` | Runtime RAM-only OLED toggle (`0` blanks & skips updates; `1` redraws) | `{"return_value": 1, "oled_enabled": true, "id": "nano_ir01", ...}` |
| `/i2cscan` | Scan I2C bus, report addresses, and redraw OLED UI | `{"found": ["0x3C"], "count": 1, "oled_detected": true, "return_value": 1, ...}` |
| `/digital/<pin>/<0\|1>` | Set digital pin output (`0..19`) | `{"message": "Pin D13 set to 1", "id": "nano_ir01", ...}` |
| `/digital/<pin>` | Read digital pin level (`0..19`) | `{"return_value": 0, "id": "nano_ir01", ...}` |
| `/mode/<pin>/<o\|i\|p>` | Set pin mode (`o`=output, `i`=input, `p`=pullup) | `{"message": "Pin D13 set to output", "id": "nano_ir01", ...}` |

### Asynchronous IR Receive Event
When a valid IR frame arrives on `D5`, the Nano emits a JSON event over Serial and updates OLED Line 3:

```json
{"event": "ir_rx", "prot": 8, "protocol": "NEC", "addr": 0, "cmd": 12, "flags": 0, "raw": 4079288079, "id": "nano_ir01"}
```

---

## 4. Automated Smoke Test

```bash
uv run --with pyserial python3 scripts/smoke_test.py --port /dev/cu.usbserial-1320
```
