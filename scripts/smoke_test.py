#!/usr/bin/env python3
"""Automated Serial API smoke test for nano_ir_remote_simulator (Arduino Nano)."""

import argparse
import glob
import json
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Run: uv run --with pyserial scripts/smoke_test.py", file=sys.stderr)
    sys.exit(2)


def detect_port() -> str:
    for pattern in ("/dev/cu.usbserial-*", "/dev/cu.usbmodem*", "/dev/ttyUSB*", "/dev/ttyACM*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    raise RuntimeError("No USB serial port found. Pass --port explicitly.")


def send_cmd(ser: serial.Serial, cmd: str, timeout: float = 2.0) -> dict:
    ser.reset_input_buffer()
    ser.write((cmd.strip() + "\r\n").encode("ascii"))
    ser.flush()

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if not text:
            continue
        try:
            return json.loads(text)
        except json.JSONDecodeError as exc:
            raise AssertionError(f"Non-JSON response for '{cmd}': {text!r}") from exc
    raise TimeoutError(f"No response for '{cmd}' within {timeout}s")


def run_smoke_tests(port: str, baud: int = 115200) -> None:
    print(f"Opening {port} at {baud} baud...")
    ser = serial.Serial(port, baud, timeout=0.5)
    try:
        # Arduino Nano resets on DTR open; wait for bootloader + 250ms OLED boot delay
        time.sleep(2.5)
        boot_noise = ser.read_all()
        if boot_noise:
            decoded = boot_noise.decode("utf-8", errors="replace").strip()
            if decoded:
                raise AssertionError(f"Unexpected boot chatter when debug=0: {decoded!r}")
        print("[PASS] Silent boot (zero non-JSON chatter)")

        # 1. Root status & /status
        root = send_cmd(ser, "/")
        assert "variables" in root and "id" in root and root.get("connected") is True, root
        orig_id = root["id"]
        print(f"[PASS] / -> id={orig_id}, oled={root.get('oled')}, debug={root.get('debug')}")

        status = send_cmd(ser, "/status")
        assert status["id"] == orig_id and "variables" in status, status
        print("[PASS] /status")

        # 2. /i2cscan
        scan = send_cmd(ser, "/i2cscan", timeout=3.0)
        assert scan.get("return_value") == 1 and isinstance(scan.get("found"), list), scan
        print(f"[PASS] /i2cscan -> found={scan['found']}, oled_detected={scan.get('oled_detected')}")

        # 3. /setdebug
        r = send_cmd(ser, "/setdebug?enable=1")
        assert r.get("return_value") == 1, r
        r = send_cmd(ser, "/setdebug?enable=0")
        assert r.get("return_value") == 1, r
        print("[PASS] /setdebug?enable=1 and /setdebug?enable=0")

        # 4. /clearkeydetail & /readkeydetail
        r = send_cmd(ser, "/clearkeydetail")
        assert r.get("return_value") == 1, r
        rk = send_cmd(ser, "/readkeydetail")
        assert rk["variables"]["rckeyprot"] == 0, rk
        print("[PASS] /clearkeydetail & /readkeydetail")

        # 5. /sendircode (NEC addr=0x00, cmd=0x0C) & /replaylast
        tx = send_cmd(ser, "/sendircode?prot=8&addr=0&cmd=12&flags=0")
        assert tx.get("return_value") == 1, tx
        time.sleep(0.1)
        rp = send_cmd(ser, "/replaylast")
        assert rp.get("return_value") == 1, rp
        print("[PASS] /sendircode & /replaylast")

        # 6. /digital/13/1 and /digital/13/0
        d1 = send_cmd(ser, "/digital/13/1")
        assert "Pin D13 set to 1" in d1.get("message", ""), d1
        d0 = send_cmd(ser, "/digital/13/0")
        assert "Pin D13 set to 0" in d0.get("message", ""), d0
        print("[PASS] /digital/13/1 and /digital/13/0")

        # 7. /id persistence across reboot
        test_id = "nano_test99"
        r = send_cmd(ser, f"/id?devid={test_id}")
        assert r.get("return_value") == 1 and r.get("id") == test_id, r
        r_get = send_cmd(ser, "/id")
        assert r_get.get("id") == test_id, r_get
        print(f"[PASS] /id set to {test_id}; rebooting Nano to verify EEPROM persistence...")

        # Reboot by closing and reopening serial (toggles DTR)
        ser.close()
        time.sleep(0.3)
        ser = serial.Serial(port, baud, timeout=0.5)
        time.sleep(2.5)
        ser.reset_input_buffer()

        after_reboot = send_cmd(ser, "/id")
        assert after_reboot.get("id") == test_id, f"Expected {test_id} after reboot, got {after_reboot}"
        print(f"[PASS] /id persisted across reboot: {after_reboot['id']}")

        # Restore original ID (or default nano_ir01)
        restore_id = orig_id if orig_id else "nano_ir01"
        r_rest = send_cmd(ser, f"/id?devid={restore_id}")
        assert r_rest.get("return_value") == 1 and r_rest.get("id") == restore_id, r_rest
        print(f"[PASS] Restored /id to {restore_id}")

        print("\nALL SERIAL API SMOKE TESTS PASSED.")
    finally:
        if ser and ser.is_open:
            ser.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Smoke test nano_ir_remote_simulator Serial API.")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    port = args.port or detect_port()
    run_smoke_tests(port, args.baud)


if __name__ == "__main__":
    main()
