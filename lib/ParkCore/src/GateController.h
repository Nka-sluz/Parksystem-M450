#pragma once
// Logik des Schranken-Picos (Pico 2): Ein- und Ausfahrt, Durchfahrt,
// 30-s-Timeout, Fail-safe und Nachmelden einer Ausfahrt.
//
// Die Klasse ruft keine Hardware-Funktionen auf. Schranke, LED, Kasse und Zeit
// werden über den Konstruktor übergeben (Dependency Injection): auf dem Pico
// die echten Implementierungen, im Test Test Doubles (test/host/TestDoubles.h).
//
// Zeigen Einfahrt und Ausfahrt auf dieselbe Barrier (ein gemeinsamer Servo,
// cfg::SERVO_COUNT = 1), darf jeweils nur eine Richtung die Schranke benutzen.

#include <stddef.h>
#include <stdint.h>

#include "Barrier.h"
#include "Clock.h"
#include "Config.h"
#include "Gate.h"
#include "Kasse.h"
#include "Lamp.h"

namespace park {

class GateController {
public:
    static constexpr size_t RID_LEN = 32;

    GateController(Barrier& entryBarrier, Barrier& exitBarrier, Lamp& lamp, Kasse& kasse,
                   const Clock& clock);

    // Präfix der Request-IDs (auf dem Pico eine Zufallszahl pro Start).
    void setBootId(uint32_t bootId) { bootId_ = bootId; }

    // Aktionen der Kamera-Simulation. Das Ergebnis steht in der Meldung der
    // jeweiligen Fahrspur (entryGate().message() / exitGate().message()).
    void onEntryPlate(const char* plate);  // Kennzeichen an der Einfahrt erkannt
    void onEntryManual();                  // manuelle Abfertigung (Ersatz-ID)
    void onEntryPassed();                  // Durchfahrt bestätigt
    void onExitPlate(const char* plate);
    void onExitManual(const char* id);     // Ausfahrt mit Ersatz-ID MANUAL-<n>
    void onExitPassed();

    // Hintergrundaufgaben (aus loop()):
    // schliesst Schranken nach cfg::GATE_OPEN_TIMEOUT_MS ohne Durchfahrt.
    void checkTimeouts();
    // Meldet eine Ausfahrt nach, die die Kasse noch nicht quittiert hat.
    // Liefert true, wenn sie jetzt quittiert wurde.
    bool retryPendingExit();

    const Gate& entryGate() const { return entry_; }
    const Gate& exitGate() const { return exit_; }
    bool anyOpen() const { return entry_.isOpen() || exit_.isOpen(); }
    bool sharedBarrier() const { return &entryBarrier_ == &exitBarrier_; }
    // Kennzeichen der noch nicht quittierten Ausfahrt ("" = keine).
    const char* pendingExit() const { return pendingExit_; }

private:
    void doEntry(bool manual, const char* plate);
    void doExit(bool manual, const char* plate);
    bool rejectIfBarrierBusy(Gate& lane);
    static void setBarrier(Barrier& barrier, bool& isOpen, bool open);
    void updateOutputs();
    void newRid(char* out, size_t len);

    Barrier& entryBarrier_;
    Barrier& exitBarrier_;
    Lamp& lamp_;
    Kasse& kasse_;
    const Clock& clock_;

    Gate entry_{"Einfahrt"};
    Gate exit_{"Ausfahrt"};
    bool entryBarrierOpen_ = false;
    bool exitBarrierOpen_ = false;
    bool lampOn_ = false;

    uint32_t bootId_ = 0;
    uint32_t ridCounter_ = 0;
    char pendingExit_[cfg::ID_BUF_LEN] = "";
    bool pendingExitManual_ = false;
    char pendingRid_[RID_LEN] = "";
};

}  // namespace park
