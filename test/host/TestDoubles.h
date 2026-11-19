#pragma once
// Test Doubles für die Abhängigkeiten der Logik (Begründung: README.md,
// Abschnitt "Test Doubles").

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Barrier.h"
#include "Clock.h"
#include "Kasse.h"
#include "Lamp.h"
#include "ParkingSystem.h"

// ----------------------------------------------------------------------------
//  Zeit → Fake
// ----------------------------------------------------------------------------

// Funktionierende Uhr, die nur weiterläuft, wenn der Test sie vorstellt.
class FakeClock : public park::Clock {
public:
    uint32_t nowMs() const override { return now_; }
    void set(uint32_t ms) { now_ = ms; }
    void advance(uint32_t ms) { now_ += ms; }

private:
    uint32_t now_ = 0;
};

// ----------------------------------------------------------------------------
//  Schranke (Servo) → Mock
// ----------------------------------------------------------------------------

// Der Test legt mit expect() vorher fest, welche Aufrufe in welcher
// Reihenfolge erwartet werden (z. B. "open,close"); verify() vergleicht sie mit
// den tatsächlichen Aufrufen.
class BarrierMock : public park::Barrier {
public:
    void open() override {
        record("open");
        open_ = true;
    }
    void close() override {
        record("close");
        open_ = false;
    }

    void expect(const char* calls) { snprintf(expected_, sizeof(expected_), "%s", calls); }
    bool verify() const { return strcmp(expected_, actual_) == 0; }
    const char* calls() const { return actual_; }
    const char* expected() const { return expected_; }
    bool isOpen() const { return open_; }

private:
    void record(const char* call) {
        const size_t len = strlen(actual_);
        snprintf(actual_ + len, sizeof(actual_) - len, "%s%s", len > 0 ? "," : "", call);
    }

    char expected_[128] = "";
    char actual_[128] = "";
    bool open_ = false;
};

// ----------------------------------------------------------------------------
//  LED → Spy bzw. Dummy
// ----------------------------------------------------------------------------

// Zeichnet auf, wie die LED geschaltet wurde; der Test fragt es danach ab.
class LampSpy : public park::Lamp {
public:
    void set(bool on) override {
        on_ = on;
        ++switches_;
    }
    bool isOn() const { return on_; }
    int switches() const { return switches_; }

private:
    bool on_ = false;
    int switches_ = 0;
};

// Wird nur übergeben, weil der Konstruktor eine LED verlangt; die Tests, die
// ihn verwenden, prüfen die LED nicht.
class LampDummy : public park::Lamp {
public:
    void set(bool) override {}
};

// ----------------------------------------------------------------------------
//  Kasse (WLAN/HTTP zu Pico 1) → Stub bzw. Fake
// ----------------------------------------------------------------------------

// Liefert die vom Test vorgegebene Antwort der Kasse, ohne Netzwerk.
// reachable = false simuliert einen Verbindungsausfall (keine Antwort).
// Merkt sich ausserdem Anzahl und Request-ID der Ausfahrtsmeldungen, damit
// Tests prüfen können, ob eine Meldung (mit derselben ID) wiederholt wird.
class KasseStub : public park::Kasse {
public:
    static park::KasseReply accept(const char* id) {
        park::KasseReply r{};
        r.accepted = true;
        snprintf(r.id, sizeof(r.id), "%s", id);
        return r;
    }
    static park::KasseReply deny(park::Reason reason, const char* id, cfg::Money due = 0) {
        park::KasseReply r{};
        r.reason = reason;
        r.due = due;
        snprintf(r.id, sizeof(r.id), "%s", id);
        return r;
    }

    bool reachable = true;
    park::KasseReply reply{};  // Antwort auf Einfahrts- und Ausfahrtsanfragen

    int requests = 0;
    int confirms = 0;
    int aborts = 0;
    char lastConfirmRid[32] = "";

    bool requestEntry(const char*, const char*, park::KasseReply& r) override { return answer(r); }
    bool requestManualEntry(const char*, park::KasseReply& r) override { return answer(r); }
    bool checkExit(const char*, bool, const char*, park::KasseReply& r) override { return answer(r); }
    bool confirmExit(const char*, bool, const char* rid) override {
        ++confirms;
        snprintf(lastConfirmRid, sizeof(lastConfirmRid), "%s", rid);
        return reachable;
    }
    bool abortExit(const char*, bool, const char*) override {
        ++aborts;
        return reachable;
    }

private:
    bool answer(park::KasseReply& r) {
        ++requests;
        if (!reachable) return false;
        r = reply;
        return true;
    }
};

// Funktionierende Kasse ohne Netzwerk: ruft direkt die echte Logik von Pico 1
// (park::ParkingSystem) auf – so wie Pico 1 es bei einer HTTP-Anfrage tut.
// Damit laufen Pico 2 und Pico 1 zusammen in einem Test.
class KasseFake : public park::Kasse {
public:
    explicit KasseFake(park::ParkingSystem& parking) : parking_(parking) {}

    bool reachable = true;

    bool requestEntry(const char* plate, const char*, park::KasseReply& reply) override {
        if (!reachable) return false;
        return entryReply(parking_.requestEntry(plate), reply);
    }
    bool requestManualEntry(const char*, park::KasseReply& reply) override {
        if (!reachable) return false;
        return entryReply(parking_.requestManualEntry(), reply);
    }
    bool checkExit(const char* id, bool manual, const char*, park::KasseReply& reply) override {
        if (!reachable) return false;
        const park::ExitResult r = parking_.requestExit(id, manual);
        reply = park::KasseReply{};
        reply.accepted = r.open;
        reply.reason = r.reason;
        reply.due = r.due;
        snprintf(reply.id, sizeof(reply.id), "%s", r.id);
        return true;
    }
    bool confirmExit(const char* id, bool manual, const char*) override {
        if (!reachable) return false;
        parking_.confirmExit(id, manual);
        return true;
    }
    bool abortExit(const char* id, bool manual, const char*) override {
        if (!reachable) return false;
        parking_.abortExit(id, manual);
        return true;
    }

private:
    static bool entryReply(const park::EntryResult& r, park::KasseReply& reply) {
        reply = park::KasseReply{};
        reply.accepted = r.granted;
        reply.reason = r.reason;
        snprintf(reply.id, sizeof(reply.id), "%s", r.id);
        return true;
    }

    park::ParkingSystem& parking_;
};
