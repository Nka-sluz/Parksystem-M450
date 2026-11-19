#pragma once
// Echte Ausgänge von Pico 2: SG90-Servo als Schranke und Onboard-LED.

#include <Arduino.h>
#include <Servo.h>

#include "Barrier.h"
#include "Config.h"
#include "Lamp.h"

class ServoBarrier : public park::Barrier {
public:
    // Richtet den Servo-Ausgang ein und schliesst die Schranke.
    bool begin(uint8_t pin) {
        pin_ = pin;
        ok_ = servo_.attach(pin, cfg::SERVO_MIN_US, cfg::SERVO_MAX_US) >= 0;
        close();
        return ok_;
    }

    void open() override { servo_.write(cfg::SERVO_OPEN_DEG); }
    void close() override { servo_.write(cfg::SERVO_CLOSED_DEG); }

    bool ok() const { return ok_; }
    uint8_t pin() const { return pin_; }

private:
    Servo servo_;
    uint8_t pin_ = 0;
    bool ok_ = false;
};

class LedLamp : public park::Lamp {
public:
    void set(bool on) override { digitalWrite(LED_BUILTIN, on ? HIGH : LOW); }
};
