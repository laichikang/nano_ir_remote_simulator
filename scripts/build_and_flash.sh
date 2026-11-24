#!/usr/bin/env bash
set -euo pipefail

# Compile and flash nano_ir_remote_simulator for Arduino Nano (ATmega328P)
# Auto-detects new bootloader (arduino:avr:nano, 115200) vs old bootloader (arduino:avr:nano:cpu=atmega328old, 57600)

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_DIR="${BASE_DIR}/nano_ir_remote_simulator"
FQBN_NEW="arduino:avr:nano"
FQBN_OLD="arduino:avr:nano:cpu=atmega328old"

detect_port() {
    local port=""
    for pattern in /dev/cu.usbserial-* /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*; do
        for match in $pattern; do
            if [[ -e "$match" ]]; then
                port="$match"
                break 2
            fi
        done
    done
    if [[ -z "$port" ]]; then
        echo "ERROR: No USB serial port found. Pass a port explicitly." >&2
        exit 1
    fi
    echo "$port"
}

compile() {
    echo "=== Compiling nano_ir_remote_simulator (${FQBN_NEW}) ==="
    arduino-cli compile --fqbn "$FQBN_NEW" "$SKETCH_DIR"
    echo "=== Compiled successfully ==="
}

flash() {
    local port="${1:-$(detect_port)}"
    echo "=== Flashing nano_ir_remote_simulator to ${port} ==="
    echo "--- Trying new bootloader (${FQBN_NEW}, 115200 baud) ---"
    if arduino-cli upload -p "$port" --fqbn "$FQBN_NEW" "$SKETCH_DIR"; then
        echo "=== Flashed successfully (new bootloader) ==="
        return 0
    fi

    echo "--- New bootloader failed; retrying old bootloader (${FQBN_OLD}, 57600 baud) ---"
    arduino-cli upload -p "$port" --fqbn "$FQBN_OLD" "$SKETCH_DIR"
    echo "=== Flashed successfully (old bootloader) ==="
}

case "${1:-all}" in
    compile)
        compile
        ;;
    flash)
        flash "${2:-}"
        ;;
    all)
        compile
        flash "${2:-}"
        ;;
    *)
        echo "Usage: $0 [compile|flash|all] [port]"
        exit 1
        ;;
esac
