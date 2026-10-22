#pragma once
// Eine Schranke (Einfahrt oder Ausfahrt): SG90-Servo + Zustand + Timeout.

#include <Arduino.h>
#include <Servo.h>
#include <string.h>

#include "Config.h"

class Gate {
public:
    Gate(const char* name, uint8_t servoPin) : name_(name), pin_(servoPin) {}

    // Liefert false, wenn der Servo-Ausgang nicht eingerichtet werden konnte.
    bool begin() {
        servoOk_ = servo_.attach(pin_, cfg::SERVO_MIN_US, cfg::SERVO_MAX_US) >= 0;
        servo_.write(cfg::SERVO_CLOSED_DEG);
        return servoOk_;
    }

    bool servoOk() const { return servoOk_; }
    uint8_t pin() const { return pin_; }

    // Nur für den Schranken-Test: Servo bewegen, ohne den Zustand zu ändern.
    void moveServo(bool open) { servo_.write(open ? cfg::SERVO_OPEN_DEG : cfg::SERVO_CLOSED_DEG); }

    void open(const char* id, bool manual, uint32_t nowMs) {
        strncpy(id_, id, sizeof(id_) - 1);
        id_[sizeof(id_) - 1] = '\0';
        manual_ = manual;
        openedAtMs_ = nowMs;
        open_ = true;
        servo_.write(cfg::SERVO_OPEN_DEG);
    }

    void close() {
        open_ = false;
        servo_.write(cfg::SERVO_CLOSED_DEG);
    }

    bool isOpen() const { return open_; }
    bool timedOut(uint32_t nowMs) const {
        return open_ && nowMs - openedAtMs_ >= cfg::GATE_OPEN_TIMEOUT_MS;
    }
    uint32_t remainingMs(uint32_t nowMs) const {
        if (!open_) return 0;
        const uint32_t elapsed = nowMs - openedAtMs_;
        return elapsed >= cfg::GATE_OPEN_TIMEOUT_MS ? 0 : cfg::GATE_OPEN_TIMEOUT_MS - elapsed;
    }

    const char* name() const { return name_; }
    const char* id() const { return id_; }
    bool manual() const { return manual_; }

    // Letzte Meldung für die Weboberfläche.
    void setMessage(const char* cls, const char* text) {
        cls_ = cls;
        strncpy(message_, text, sizeof(message_) - 1);
        message_[sizeof(message_) - 1] = '\0';
        ++messageSeq_;
    }
    const char* messageClass() const { return cls_; }
    const char* message() const { return message_; }
    uint32_t messageSeq() const { return messageSeq_; }

private:
    const char* name_;
    uint8_t pin_;
    Servo servo_;
    bool servoOk_ = false;
    bool open_ = false;
    bool manual_ = false;
    uint32_t openedAtMs_ = 0;
    char id_[cfg::ID_BUF_LEN] = "";
    const char* cls_ = "ok";
    char message_[160] = "";
    uint32_t messageSeq_ = 0;
};
