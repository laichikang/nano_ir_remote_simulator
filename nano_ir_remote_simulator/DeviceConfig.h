#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>

// --- Pin Definitions (Arduino Nano ATmega328P) ---
#define IR_RECEIVE_PIN   5    // D5: Active-LOW demodulated IR receiver
#define IR_SEND_PIN      6    // D6: 38 kHz IR blaster (IRremote v4 software PWM)
#define STATUS_LED_PIN   13   // D13 (LED_BUILTIN): TX/RX activity LED

// Hardware I2C on ATmega328P: SDA = A4 (D18), SCL = A5 (D19)
#define OLED_I2C_ADDR    0x3C
#define OLED_BOOT_DELAY_MS 250
#define OLED_INIT_RETRIES  3
#define OLED_RETRY_DELAY_MS 50

// --- Serial & Buffer Limits ---
#define SERIAL_BAUD_RATE 115200
#define CMD_BUFFER_SIZE  96
#define DEVICE_ID_MAX_LEN 20
#define DEFAULT_DEVICE_ID "nano_ir01"
#define DEVICE_NAME       "nano_ir"

// --- Nano GPIO Range (D0-D13, A0-A5 as 14-19) ---
#define NANO_MAX_PIN     19

// --- Internal EEPROM Layout (ATmega328P 1 KB EEPROM) ---
#define EEPROM_MAGIC_0   0x4E // 'N'
#define EEPROM_MAGIC_1   0x41 // 'A'
#define EEPROM_MAGIC_2   0x4E // 'N'
#define EEPROM_MAGIC_3   0x4F // 'O'
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_LEN   4
#define EEPROM_ADDR_ID    5

// --- IRremote v4.x Compile-Time Configuration (2 KB SRAM / 32 KB Flash) ---
#define RAW_BUFFER_LENGTH 100
#define EXCLUDE_EXOTIC_PROTOCOLS
#define NO_LED_FEEDBACK_CODE

// --- Protocol Data Struct ---
struct IrFrame {
    uint8_t  protocol;
    uint16_t address;
    uint16_t command;
    uint8_t  flags;
    uint32_t raw;
};

#endif // DEVICE_CONFIG_H
