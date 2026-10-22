#pragma once
// Logischer Zustand einer Fahrspur (Einfahrt oder Ausfahrt): offen/zu,
// Kennzeichen, Timeout und letzte Meldung. Welcher Servo sich dazu bewegt,
// entscheidet main.cpp (ein gemeinsamer oder zwei getrennte Servos, siehe
// cfg::SERVO_COUNT).

#include <Arduino.h>
#include <string.h>

#include "Config.h"

class Gate {
public:
    explicit Gate(const char* name) : name_(name) {}

    void open(const char* id, bool manual, uint32_t nowMs) {
        strncpy(id_, id, sizeof(id_) - 1);
        id_[sizeof(id_) - 1] = '\0';
        manual_ = manual;
        openedAtMs_ = nowMs;
        open_ = true;
    }

    void close() { open_ = false; }

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
    }
    const char* messageClass() const { return cls_; }
    const char* message() const { return message_; }

private:
    const char* name_;
    bool open_ = false;
    bool manual_ = false;
    uint32_t openedAtMs_ = 0;
    char id_[cfg::ID_BUF_LEN] = "";
    const char* cls_ = "ok";
    char message_[160] = "";
};
