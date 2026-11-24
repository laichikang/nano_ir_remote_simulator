#include <Arduino.h>
#include "DeviceConfig.h"
#include <IRremote.hpp>
#include <EEPROM.h>
#include <Wire.h>
#include "OledUi.h"

// --- State ---
static OledUi oled_ui;
static char device_id[DEVICE_ID_MAX_LEN + 1] = DEFAULT_DEVICE_ID;
static bool debug_enabled = false;

// Pending IR capture mailbox (aREST /readkeydetail & / compatibility)
static IrFrame mailbox = {0, 0, 0, 0, 0};
static bool has_pending_capture = false;

// Last TX / RX frames for /replaylast
static IrFrame last_tx = {0, 0, 0, 0, 0};
static IrFrame last_rx = {0, 0, 0, 0, 0};

// Fixed-size serial command buffer (no String heap fragmentation)
static char cmd_buf[CMD_BUFFER_SIZE];
static uint8_t cmd_len = 0;

// --- Protocol Name Helper (RAM-safe, compact for 21-char OLED) ---
static const char* protocol_short_name(uint8_t prot, char* fallback_buf, size_t fallback_sz) {
    switch ((decode_type_t)prot) {
        case NEC:          return "NEC";
        case NEC2:         return "NEC2";
        case ONKYO:        return "ONKY";
        case APPLE:        return "APPL";
        case SAMSUNG:      return "SMSG";
        case SAMSUNG48:    return "SM48";
        case SAMSUNGLG:    return "SMLG";
        case SONY:         return "SONY";
        case RC5:          return "RC5";
        case RC6:          return "RC6";
        case LG:           return "LG";
        case JVC:          return "JVC";
        case PANASONIC:    return "PANA";
        case KASEIKYO:     return "KASE";
        case SHARP:        return "SHRP";
        case DENON:        return "DENO";
        case PULSE_DISTANCE: return "PDST";
        case PULSE_WIDTH:  return "PWID";
        default:
            snprintf(fallback_buf, fallback_sz, "P%u", prot);
            return fallback_buf;
    }
}

// --- Status LED Pulse Helper ---
static void pulse_status_led(uint8_t ms) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(ms);
    digitalWrite(STATUS_LED_PIN, LOW);
}

// --- Internal EEPROM Persistence (ATmega328P) ---
static void save_config() {
    EEPROM.update(EEPROM_ADDR_MAGIC + 0, EEPROM_MAGIC_0);
    EEPROM.update(EEPROM_ADDR_MAGIC + 1, EEPROM_MAGIC_1);
    EEPROM.update(EEPROM_ADDR_MAGIC + 2, EEPROM_MAGIC_2);
    EEPROM.update(EEPROM_ADDR_MAGIC + 3, EEPROM_MAGIC_3);
    uint8_t len = (uint8_t)strlen(device_id);
    if (len > DEVICE_ID_MAX_LEN) len = DEVICE_ID_MAX_LEN;
    EEPROM.update(EEPROM_ADDR_LEN, len);
    for (uint8_t i = 0; i < len; i++) {
        EEPROM.update(EEPROM_ADDR_ID + i, (uint8_t)device_id[i]);
    }
}

static void load_config() {
    if (EEPROM.read(EEPROM_ADDR_MAGIC + 0) == EEPROM_MAGIC_0 &&
        EEPROM.read(EEPROM_ADDR_MAGIC + 1) == EEPROM_MAGIC_1 &&
        EEPROM.read(EEPROM_ADDR_MAGIC + 2) == EEPROM_MAGIC_2 &&
        EEPROM.read(EEPROM_ADDR_MAGIC + 3) == EEPROM_MAGIC_3) {
        uint8_t len = EEPROM.read(EEPROM_ADDR_LEN);
        if (len > 0 && len <= DEVICE_ID_MAX_LEN) {
            for (uint8_t i = 0; i < len; i++) {
                device_id[i] = (char)EEPROM.read(EEPROM_ADDR_ID + i);
            }
            device_id[len] = '\0';
            return;
        }
    }
    strncpy(device_id, DEFAULT_DEVICE_ID, DEVICE_ID_MAX_LEN);
    device_id[DEVICE_ID_MAX_LEN] = '\0';
    save_config();
}

// --- Query String Parsers (Zero Heap Allocation) ---
static bool get_param_str(const char* query, const char* key, char* out, size_t out_sz) {
    size_t key_len = strlen(key);
    const char* p = query;
    while (*p) {
        const char* found = strstr(p, key);
        if (!found) return false;
        bool valid_boundary = (found == query) ||
                              (*(found - 1) == '&') ||
                              (*(found - 1) == '?') ||
                              (*(found - 1) == ' ');
        if (valid_boundary && found[key_len] == '=') {
            const char* val_start = found + key_len + 1;
            const char* val_end = val_start;
            while (*val_end && *val_end != '&' && *val_end != ' ' && *val_end != '\r' && *val_end != '\n') {
                val_end++;
            }
            size_t len = (size_t)(val_end - val_start);
            if (len >= out_sz) len = out_sz - 1;
            memcpy(out, val_start, len);
            out[len] = '\0';
            return true;
        }
        p = found + 1;
    }
    return false;
}

static long get_param_long(const char* query, const char* key, long default_val) {
    char val_buf[16];
    if (!get_param_str(query, key, val_buf, sizeof(val_buf)) || val_buf[0] == '\0') {
        return default_val;
    }
    return strtol(val_buf, NULL, 0);
}

// --- JSON Response Helpers (F() macros to keep strings in Flash) ---
static void print_json_tail() {
    Serial.print(F(", \"id\": \""));
    Serial.print(device_id);
    Serial.print(F("\", \"name\": \""));
    Serial.print(F(DEVICE_NAME));
    Serial.println(F("\", \"hardware\": \"arduino\", \"connected\": true}"));
}

static void send_return_value(int val) {
    Serial.print(F("{\"return_value\": "));
    Serial.print(val);
    print_json_tail();
}

static void send_status_json() {
    Serial.print(F("{\"variables\": {\"rckeyprot\": "));
    Serial.print(mailbox.protocol);
    Serial.print(F(", \"rckeyaddr\": "));
    Serial.print(mailbox.address);
    Serial.print(F(", \"rckeycmd\": "));
    Serial.print(mailbox.command);
    Serial.print(F(", \"rckeyflags\": "));
    Serial.print(mailbox.flags);
    Serial.print(F(", \"rckeyraw\": "));
    Serial.print(mailbox.raw);
    Serial.print(F("}, \"debug\": "));
    Serial.print(debug_enabled ? 1 : 0);
    Serial.print(F(", \"oled\": "));
    Serial.print(oled_ui.isReady() ? F("true") : F("false"));
    Serial.print(F(", \"oled_enabled\": "));
    Serial.print(oled_ui.isEnabled() ? F("true") : F("false"));
    print_json_tail();
}

// --- IR Transmit Helper (Stops RX before TX to prevent self-capture) ---
static void transmit_ir_frame(uint8_t prot, uint16_t addr, uint16_t cmd, uint8_t flags) {
    IRData ir_data;
    memset(&ir_data, 0, sizeof(ir_data));
    ir_data.protocol = (decode_type_t)prot;
    ir_data.address = addr;
    ir_data.command = cmd;
    ir_data.flags = flags;
    int_fast8_t repeats = (flags & IRDATA_FLAGS_IS_REPEAT) ? -1 : 0;

    if (ir_data.protocol == SONY) {
        if (ir_data.numberOfBits == 0) {
            if (addr > 0xFF) {
                ir_data.numberOfBits = 20;
            } else if (addr > 0x1F) {
                ir_data.numberOfBits = 15;
            } else {
                ir_data.numberOfBits = 12;
            }
        }
        if (repeats < 2) {
            repeats = 2;
        }
    }

    IrReceiver.stop();
    IrSender.write(&ir_data, repeats);
    IrReceiver.start();

    last_tx.protocol = prot;
    last_tx.address = addr;
    last_tx.command = cmd;
    last_tx.flags = flags;

    char pbuf[6];
    oled_ui.setLastTx(protocol_short_name(prot, pbuf, sizeof(pbuf)), addr, cmd);
    pulse_status_led(20);

    if (debug_enabled) {
        Serial.print(F("[DEBUG] IR TX prot="));
        Serial.print(prot);
        Serial.print(F(" addr=0x"));
        Serial.print(addr, HEX);
        Serial.print(F(" cmd=0x"));
        Serial.print(cmd, HEX);
        Serial.print(F(" flags=0x"));
        Serial.println(flags, HEX);
    }
}

// --- Endpoint Handlers ---
static void handle_send_ir_code(const char* query) {
    uint8_t prot = (uint8_t)get_param_long(query, "prot", NEC);
    uint16_t addr = (uint16_t)get_param_long(query, "addr", 0);
    uint16_t cmd = (uint16_t)get_param_long(query, "cmd", 0);
    uint8_t flags = (uint8_t)get_param_long(query, "flags", 0);

    transmit_ir_frame(prot, addr, cmd, flags);
    send_return_value(1);
}

static void handle_replay_last() {
    if (last_rx.protocol != 0) {
        transmit_ir_frame(last_rx.protocol, last_rx.address, last_rx.command, last_rx.flags);
        send_return_value(1);
    } else if (last_tx.protocol != 0) {
        transmit_ir_frame(last_tx.protocol, last_tx.address, last_tx.command, last_tx.flags);
        send_return_value(1);
    } else {
        send_return_value(0);
    }
}

static void handle_clear_key_detail() {
    mailbox.protocol = 0;
    mailbox.address = 0;
    mailbox.command = 0;
    mailbox.flags = 0;
    mailbox.raw = 0;
    has_pending_capture = false;
    send_return_value(1);
}

static void update_device_id(const char* new_id) {
    size_t len = strlen(new_id);
    if (len == 0 || len > DEVICE_ID_MAX_LEN) {
        send_return_value(0);
        return;
    }
    strncpy(device_id, new_id, DEVICE_ID_MAX_LEN);
    device_id[DEVICE_ID_MAX_LEN] = '\0';
    save_config();
    oled_ui.drawHeader(device_id);
    send_return_value(1);
}

static void handle_id_endpoint(const char* cmd) {
    char new_id[DEVICE_ID_MAX_LEN + 1];
    // Support /id?devid=<id>, /id?id=<id>, /id?set=<id>, or /id/<id>
    if (get_param_str(cmd, "devid", new_id, sizeof(new_id)) ||
        get_param_str(cmd, "id", new_id, sizeof(new_id)) ||
        get_param_str(cmd, "set", new_id, sizeof(new_id))) {
        update_device_id(new_id);
        return;
    }
    if (strncmp(cmd, "/id/", 4) == 0 && cmd[4] != '\0') {
        update_device_id(cmd + 4);
        return;
    }
    // GET /id
    Serial.print(F("{\"id\": \""));
    Serial.print(device_id);
    Serial.print(F("\", \"name\": \""));
    Serial.print(F(DEVICE_NAME));
    Serial.println(F("\", \"hardware\": \"arduino\", \"connected\": true}"));
}

static void handle_setup_config(const char* cmd) {
    char new_id[DEVICE_ID_MAX_LEN + 1];
    if (get_param_str(cmd, "devid", new_id, sizeof(new_id))) {
        update_device_id(new_id);
    } else {
        send_return_value(0);
    }
}

static void handle_set_debug(const char* cmd) {
    long enable = get_param_long(cmd, "enable", -1);
    if (enable == 0 || enable == 1) {
        debug_enabled = (enable == 1);
        oled_ui.drawStatus(debug_enabled);
        send_return_value(1);
    } else {
        send_return_value(0);
    }
}

static void handle_set_oled(const char* cmd) {
    long enable = get_param_long(cmd, "enable", -1);
    if (enable == 0 || enable == 1) {
        oled_ui.setEnabled(enable == 1, device_id, debug_enabled);
        Serial.print(F("{\"return_value\": 1, \"oled_enabled\": "));
        Serial.print(oled_ui.isEnabled() ? F("true") : F("false"));
        print_json_tail();
    } else {
        send_return_value(0);
    }
}

static void handle_i2c_scan() {
    uint8_t found_count = 0;
    bool oled_found = false;

    Serial.print(F("{\"found\": ["));
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            if (found_count > 0) Serial.print(F(", "));
            char hex_buf[6];
            snprintf(hex_buf, sizeof(hex_buf), "0x%02X", addr);
            Serial.print('"');
            Serial.print(hex_buf);
            Serial.print('"');
            found_count++;
            if (addr == OLED_I2C_ADDR) {
                oled_found = true;
            }
        }
    }
    Serial.print(F("], \"count\": "));
    Serial.print(found_count);
    Serial.print(F(", \"oled_detected\": "));
    Serial.print(oled_found ? F("true") : F("false"));
    Serial.print(F(", \"return_value\": 1"));
    print_json_tail();

    // Preserve and redraw OLED UI after bus scan (ADR-025)
    if (oled_found) {
        oled_ui.reinitAndRedraw(device_id, debug_enabled);
    }
}

static void handle_digital(const char* cmd) {
    // /digital/<pin> or /digital/<pin>/<val>
    const char* p = cmd + 9; // skip "/digital/"
    char* endp = NULL;
    long pin = strtol(p, &endp, 10);
    if (endp == p || pin < 0 || pin > NANO_MAX_PIN) {
        send_return_value(0);
        return;
    }

    if (*endp == '/') {
        const char* val_str = endp + 1;
        if (*val_str == '\0' || *val_str == 'r' || *val_str == 'R') {
            send_return_value(digitalRead((uint8_t)pin));
        } else {
            int val = (atoi(val_str) != 0) ? 1 : 0;
            pinMode((uint8_t)pin, OUTPUT);
            digitalWrite((uint8_t)pin, val);
            Serial.print(F("{\"message\": \"Pin D"));
            Serial.print(pin);
            Serial.print(F(" set to "));
            Serial.print(val);
            Serial.print('"');
            print_json_tail();
        }
    } else {
        send_return_value(digitalRead((uint8_t)pin));
    }
}

static void handle_mode(const char* cmd) {
    // /mode/<pin>/<o|i|p>
    const char* p = cmd + 6; // skip "/mode/"
    char* endp = NULL;
    long pin = strtol(p, &endp, 10);
    if (endp == p || pin < 0 || pin > NANO_MAX_PIN || *endp != '/') {
        send_return_value(0);
        return;
    }
    char m = *(endp + 1);
    const __FlashStringHelper* mode_str = NULL;
    if (m == 'o' || m == 'O') {
        pinMode((uint8_t)pin, OUTPUT);
        mode_str = F("output");
    } else if (m == 'i' || m == 'I') {
        pinMode((uint8_t)pin, INPUT);
        mode_str = F("input");
    } else if (m == 'p' || m == 'P') {
        pinMode((uint8_t)pin, INPUT_PULLUP);
        mode_str = F("input with pullup");
    } else {
        send_return_value(0);
        return;
    }

    Serial.print(F("{\"message\": \"Pin D"));
    Serial.print(pin);
    Serial.print(F(" set to "));
    Serial.print(mode_str);
    Serial.print('"');
    print_json_tail();
}

// --- Command Router ---
static void dispatch_command(char* raw_cmd) {
    // Trim leading whitespace
    while (*raw_cmd == ' ' || *raw_cmd == '\t') raw_cmd++;
    size_t len = strlen(raw_cmd);
    while (len > 0 && (raw_cmd[len - 1] == ' ' || raw_cmd[len - 1] == '\t' ||
                       raw_cmd[len - 1] == '\r' || raw_cmd[len - 1] == '\n')) {
        raw_cmd[--len] = '\0';
    }
    if (len == 0) return;

    // Normalize leading slash
    char normalized[CMD_BUFFER_SIZE + 1];
    if (raw_cmd[0] != '/') {
        normalized[0] = '/';
        strncpy(normalized + 1, raw_cmd, sizeof(normalized) - 2);
        normalized[sizeof(normalized) - 1] = '\0';
    } else {
        strncpy(normalized, raw_cmd, sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = '\0';
    }

    if (strcmp(normalized, "/") == 0 ||
        strcmp(normalized, "/status") == 0 ||
        strncmp(normalized, "/status?", 8) == 0 ||
        strcmp(normalized, "/readkeydetail") == 0 ||
        strncmp(normalized, "/readkeydetail?", 15) == 0) {
        send_status_json();
    } else if (strcmp(normalized, "/clearkeydetail") == 0 ||
               strncmp(normalized, "/clearkeydetail?", 16) == 0) {
        handle_clear_key_detail();
    } else if (strncmp(normalized, "/sendircode", 11) == 0 &&
               (normalized[11] == '\0' || normalized[11] == '?')) {
        handle_send_ir_code(normalized);
    } else if (strcmp(normalized, "/replaylast") == 0 ||
               strncmp(normalized, "/replaylast?", 12) == 0) {
        handle_replay_last();
    } else if (strcmp(normalized, "/id") == 0 ||
               strncmp(normalized, "/id?", 4) == 0 ||
               strncmp(normalized, "/id/", 4) == 0) {
        handle_id_endpoint(normalized);
    } else if (strncmp(normalized, "/setupconfig", 12) == 0 &&
               (normalized[12] == '\0' || normalized[12] == '?')) {
        handle_setup_config(normalized);
    } else if (strncmp(normalized, "/setdebug", 9) == 0 &&
               (normalized[9] == '\0' || normalized[9] == '?')) {
        handle_set_debug(normalized);
    } else if (strncmp(normalized, "/setoled", 8) == 0 &&
               (normalized[8] == '\0' || normalized[8] == '?')) {
        handle_set_oled(normalized);
    } else if (strcmp(normalized, "/i2cscan") == 0 ||
               strncmp(normalized, "/i2cscan?", 9) == 0) {
        handle_i2c_scan();
    } else if (strncmp(normalized, "/digital/", 9) == 0) {
        handle_digital(normalized);
    } else if (strncmp(normalized, "/mode/", 6) == 0) {
        handle_mode(normalized);
    } else {
        // Unknown endpoint: return valid JSON error
        Serial.print(F("{\"error\": \"unknown_endpoint\", \"return_value\": 0"));
        print_json_tail();
    }
}

// --- IR Receive Handler ---
static void process_ir_receive() {
    if (!IrReceiver.decode()) return;

    decode_type_t proto = IrReceiver.decodedIRData.protocol;
    uint16_t addr = IrReceiver.decodedIRData.address;
    uint16_t cmd = IrReceiver.decodedIRData.command;
    uint8_t flags = IrReceiver.decodedIRData.flags;
    uint32_t raw = (uint32_t)IrReceiver.decodedIRData.decodedRawData;

    bool is_repeat = (flags & IRDATA_FLAGS_IS_REPEAT) != 0;
    bool is_pulse_noise = (proto == PULSE_DISTANCE || proto == PULSE_WIDTH) &&
                          (addr == 0) && (cmd == 0);

    if (proto != UNKNOWN && !is_repeat && !is_pulse_noise) {
        uint8_t prot = (uint8_t)proto;

        if (!has_pending_capture) {
            mailbox.protocol = prot;
            mailbox.address = addr;
            mailbox.command = cmd;
            mailbox.flags = flags;
            mailbox.raw = raw;
            has_pending_capture = true;
        }

        last_rx.protocol = prot;
        last_rx.address = addr;
        last_rx.command = cmd;
        last_rx.flags = flags;
        last_rx.raw = raw;

        char pbuf[6];
        const char* pname = protocol_short_name(prot, pbuf, sizeof(pbuf));

        // Emit structured JSON event over Serial (valid JSON when debug=0)
        Serial.print(F("{\"event\": \"ir_rx\", \"prot\": "));
        Serial.print(prot);
        Serial.print(F(", \"protocol\": \""));
        Serial.print(pname);
        Serial.print(F("\", \"addr\": "));
        Serial.print(addr);
        Serial.print(F(", \"cmd\": "));
        Serial.print(cmd);
        Serial.print(F(", \"flags\": "));
        Serial.print(flags);
        Serial.print(F(", \"raw\": "));
        Serial.print(raw);
        Serial.print(F(", \"id\": \""));
        Serial.print(device_id);
        Serial.println(F("\"}"));

        if (debug_enabled) {
            Serial.print(F("[DEBUG] IR RX prot="));
            Serial.print(prot);
            Serial.print(F(" addr=0x"));
            Serial.print(addr, HEX);
            Serial.print(F(" cmd=0x"));
            Serial.print(cmd, HEX);
            Serial.print(F(" flags=0x"));
            Serial.println(flags, HEX);
        }

        // Complete I2C OLED redraw, LED pulse, and UART flush before re-arming IR RX
        oled_ui.setLastRx(pname, addr, cmd);
        pulse_status_led(10);
        Serial.flush();
    }

    IrReceiver.resume();
}

// --- Arduino Lifecycle ---
void setup() {
    // Set inactive level before output mode to prevent boot glitch
    digitalWrite(STATUS_LED_PIN, LOW);
    pinMode(STATUS_LED_PIN, OUTPUT);

    // Use 2 stop bits on TX (8N2) to resync CH340 UART against 16 MHz AVR +2.1% baud error at 115200;
    // ATmega328P receiver ignores USBS0 and accepts standard 8N1 from host.
    Serial.begin(SERIAL_BAUD_RATE, SERIAL_8N2);
    load_config();

    // Cold-boot power stabilization delay before I2C OLED init (ADR-025)
    delay(OLED_BOOT_DELAY_MS);
    if (oled_ui.begin(OLED_I2C_ADDR)) {
        oled_ui.drawAll(device_id, debug_enabled);
    }

    IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
    IrSender.begin();
}

void loop() {
    // 1. Process incoming USB Serial commands
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (cmd_len > 0) {
                cmd_buf[cmd_len] = '\0';
                dispatch_command(cmd_buf);
                cmd_len = 0;
            }
        } else if (cmd_len < (CMD_BUFFER_SIZE - 1)) {
            cmd_buf[cmd_len++] = c;
        }
    }

    // 2. Non-blocking IR reception
    process_ir_receive();
}
