#ifndef OLED_UI_H
#define OLED_UI_H

#include <Arduino.h>
#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "DeviceConfig.h"

class OledUi {
public:
    OledUi() : _ready(false), _addr(OLED_I2C_ADDR) {
        _lastTx[0] = '\0';
        _lastRx[0] = '\0';
    }

    bool begin(uint8_t addr = OLED_I2C_ADDR) {
        _addr = addr;
        Wire.begin();
        Wire.setClock(100000UL);

        for (uint8_t attempt = 0; attempt < OLED_INIT_RETRIES; attempt++) {
            Wire.beginTransmission(_addr);
            if (Wire.endTransmission() == 0) {
                _oled.begin(&Adafruit128x64, _addr);
                _oled.setFont(System5x7);
                _oled.clear();
                _ready = true;
                return true;
            }
            delay(OLED_RETRY_DELAY_MS);
        }
        _ready = false;
        return false;
    }

    bool isReady() const { return _ready; }
    uint8_t address() const { return _addr; }

    void setLastTx(const char* protName, uint16_t addr, uint16_t cmd) {
        snprintf(_lastTx, sizeof(_lastTx), "TX:%s %02X:%02X", protName, addr, cmd);
        if (_ready) {
            drawLine(2, _lastTx);
        }
    }

    void setLastRx(const char* protName, uint16_t addr, uint16_t cmd) {
        snprintf(_lastRx, sizeof(_lastRx), "RX:%s %02X:%02X", protName, addr, cmd);
        if (_ready) {
            drawLine(4, _lastRx);
        }
    }

    void drawAll(const char* deviceId, bool debugEnabled) {
        if (!_ready) return;
        _oled.clear();
        drawHeader(deviceId);
        drawLine(2, _lastTx[0] ? _lastTx : "TX: ---");
        drawLine(4, _lastRx[0] ? _lastRx : "RX: ---");
        drawStatus(debugEnabled);
    }

    void drawHeader(const char* deviceId) {
        if (!_ready) return;
        char line[22];
        snprintf(line, sizeof(line), "%s READY", deviceId);
        drawLine(0, line);
    }

    void drawStatus(bool debugEnabled) {
        if (!_ready) return;
        char line[22];
        snprintf(line, sizeof(line), "DBG:%d I2C:0x%02X OK", debugEnabled ? 1 : 0, _addr);
        drawLine(6, line);
    }

    bool reinitAndRedraw(const char* deviceId, bool debugEnabled) {
        bool ok = begin(_addr);
        if (ok) {
            drawAll(deviceId, debugEnabled);
        }
        return ok;
    }

private:
    void drawLine(uint8_t row, const char* text) {
        _oled.setCursor(0, row);
        _oled.print(text);
        _oled.clearToEOL();
    }

    SSD1306AsciiWire _oled;
    bool _ready;
    uint8_t _addr;
    char _lastTx[22];
    char _lastRx[22];
};

#endif // OLED_UI_H
