#include "GateController.h"

#include <stdio.h>
#include <string.h>

#include "Format.h"

namespace park {

GateController::GateController(Barrier& entryBarrier, Barrier& exitBarrier, Lamp& lamp, Kasse& kasse,
                               const Clock& clock)
    : entryBarrier_(entryBarrier), exitBarrier_(exitBarrier), lamp_(lamp), kasse_(kasse), clock_(clock) {}

void GateController::newRid(char* out, size_t len) {
    snprintf(out, len, "p2-%08lx-%lu", static_cast<unsigned long>(bootId_),
             static_cast<unsigned long>(++ridCounter_));
}

// Steuert die Schranke nur bei einer Änderung an.
void GateController::setBarrier(Barrier& barrier, bool& isOpen, bool open) {
    if (isOpen == open) return;
    isOpen = open;
    if (open) {
        barrier.open();
    } else {
        barrier.close();
    }
}

// Bringt Schranke(n) und LED in den Zustand der beiden Fahrspuren. Eine
// gemeinsame Schranke ist offen, solange eine der beiden Spuren offen ist.
void GateController::updateOutputs() {
    const bool entryOpen = entry_.isOpen();
    const bool exitOpen = exit_.isOpen();
    const bool entryTarget = sharedBarrier() ? (entryOpen || exitOpen) : entryOpen;
    setBarrier(entryBarrier_, entryBarrierOpen_, entryTarget);
    if (!sharedBarrier()) setBarrier(exitBarrier_, exitBarrierOpen_, exitOpen);

    const bool on = entryOpen || exitOpen;
    if (lampOn_ != on) {
        lampOn_ = on;
        lamp_.set(on);
    }
}

// Bei nur einer Schranke darf jeweils nur eine Richtung sie benutzen.
bool GateController::rejectIfBarrierBusy(Gate& lane) {
    if (!sharedBarrier()) return false;
    const Gate& other = &lane == &entry_ ? exit_ : entry_;
    if (!other.isOpen()) return false;
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Schranke gerade für die %s von %s offen – bitte warten, bis dort die Durchfahrt "
             "bestätigt ist",
             other.name(), other.id());
    lane.setMessage("warn", msg);
    return true;
}

// ----------------------------------------------------------------------------
//  Einfahrt
// ----------------------------------------------------------------------------

void GateController::onEntryPlate(const char* plate) { doEntry(false, plate); }
void GateController::onEntryManual() { doEntry(true, nullptr); }

void GateController::doEntry(bool manual, const char* plate) {
    if (entry_.isOpen()) {
        entry_.setMessage("warn", "Einfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        return;
    }
    if (rejectIfBarrierBusy(entry_)) return;

    char rid[RID_LEN];
    newRid(rid, sizeof(rid));
    KasseReply reply{};
    const bool answered = manual ? kasse_.requestManualEntry(rid, reply)
                                 : kasse_.requestEntry(plate, rid, reply);
    if (!answered) {
        entry_.setMessage("err", "Keine Verbindung zur Kasse – Schranke bleibt geschlossen. "
                                 "Bitte erneut versuchen.");
        return;
    }

    char msg[160];
    if (reply.accepted) {
        entry_.open(reply.id, manual, clock_.nowMs());
        updateOutputs();
        if (manual) {
            snprintf(msg, sizeof(msg), "Manuelle Abfertigung: Ersatz-ID %s (bitte notieren) – Einfahrt offen",
                     reply.id);
        } else {
            snprintf(msg, sizeof(msg), "Willkommen %s – Einfahrt offen", reply.id);
        }
        entry_.setMessage("ok", msg);
        return;
    }

    switch (reply.reason) {
        case Reason::Invalid:
            snprintf(msg, sizeof(msg),
                     "Kennzeichen ungültig – Schranke bleibt geschlossen. Manuelle Abfertigung möglich.");
            break;
        case Reason::Duplicate:
            snprintf(msg, sizeof(msg), "%s: bereits eingefahren – Schranke bleibt geschlossen", reply.id);
            break;
        case Reason::Full:
            snprintf(msg, sizeof(msg), "Parkhaus voll – Schranke bleibt geschlossen");
            break;
        default:
            snprintf(msg, sizeof(msg), "Einfahrt verweigert: %s", reasonMessage(reply.reason));
    }
    entry_.setMessage("err", msg);
}

void GateController::onEntryPassed() {
    if (!entry_.isOpen()) {
        entry_.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
        return;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Einfahrt zu", entry_.id());
    entry_.close();
    updateOutputs();
    entry_.setMessage("ok", msg);
}

// ----------------------------------------------------------------------------
//  Ausfahrt
// ----------------------------------------------------------------------------

void GateController::onExitPlate(const char* plate) { doExit(false, plate); }
void GateController::onExitManual(const char* id) { doExit(true, id); }

void GateController::doExit(bool manual, const char* plate) {
    if (exit_.isOpen()) {
        exit_.setMessage("warn", "Ausfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        return;
    }
    if (rejectIfBarrierBusy(exit_)) return;
    if (manual && plate[0] == '\0') {
        exit_.setMessage("err", "Manuelle Abfertigung: bitte Ersatz-ID (z. B. MANUAL-1) eingeben");
        return;
    }

    char rid[RID_LEN];
    newRid(rid, sizeof(rid));
    KasseReply reply{};
    if (!kasse_.checkExit(plate, manual, rid, reply)) {
        exit_.setMessage("err", "Keine Verbindung zur Kasse – Ausfahrt bleibt geschlossen "
                                "(Fail-safe). Bitte erneut versuchen.");
        return;
    }

    char msg[160];
    if (reply.accepted) {
        exit_.open(reply.id, manual, clock_.nowMs());
        updateOutputs();
        snprintf(msg, sizeof(msg), "Gute Fahrt %s – Ausfahrt offen", reply.id);
        exit_.setMessage("ok", msg);
        return;
    }

    char due[16];
    formatMoney(reply.due, due, sizeof(due));
    switch (reply.reason) {
        case Reason::Unpaid:
            snprintf(msg, sizeof(msg), "Bitte zuerst bezahlen – offener Betrag CHF %s (%s)", due, reply.id);
            break;
        case Reason::Unknown:
            snprintf(msg, sizeof(msg),
                     "%s ist nicht im Register – bitte manuelle Abfertigung "
                     "(Ersatz-ID eingeben oder Personal rufen)",
                     reply.id);
            break;
        case Reason::Exited:
            snprintf(msg, sizeof(msg), "%s ist bereits ausgefahren – bitte manuelle Abfertigung", reply.id);
            break;
        case Reason::Invalid:
            snprintf(msg, sizeof(msg), "Kennzeichen ungültig – bitte manuelle Abfertigung");
            break;
        default:
            snprintf(msg, sizeof(msg), "Ausfahrt verweigert: %s", reasonMessage(reply.reason));
    }
    exit_.setMessage("err", msg);
}

void GateController::onExitPassed() {
    if (!exit_.isOpen()) {
        exit_.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
        return;
    }

    char id[cfg::ID_BUF_LEN];
    strncpy(id, exit_.id(), sizeof(id));
    const bool manual = exit_.manual();
    exit_.close();
    updateOutputs();

    char rid[RID_LEN];
    newRid(rid, sizeof(rid));
    char msg[160];
    if (kasse_.confirmExit(id, manual, rid)) {
        snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Ausfahrt zu", id);
        exit_.setMessage("ok", msg);
    } else {
        // Fahrzeug ist bereits durch – Meldung an die Kasse wird mit derselben
        // Request-ID nachgeholt.
        strncpy(pendingExit_, id, sizeof(pendingExit_));
        strncpy(pendingRid_, rid, sizeof(pendingRid_));
        pendingExitManual_ = manual;
        snprintf(msg, sizeof(msg),
                 "Durchfahrt %s bestätigt – Ausfahrt zu. Kasse nicht erreichbar, Meldung wird wiederholt.",
                 id);
        exit_.setMessage("warn", msg);
    }
}

bool GateController::retryPendingExit() {
    if (pendingExit_[0] == '\0') return false;
    if (!kasse_.confirmExit(pendingExit_, pendingExitManual_, pendingRid_)) return false;
    pendingExit_[0] = '\0';
    return true;
}

// ----------------------------------------------------------------------------
//  Timeout
// ----------------------------------------------------------------------------

void GateController::checkTimeouts() {
    const uint32_t now = clock_.nowMs();
    if (entry_.timedOut(now)) {
        entry_.close();
        updateOutputs();
        entry_.setMessage("warn", "Keine Durchfahrt innerhalb von 30 s – Einfahrt geschlossen");
    }
    if (exit_.timedOut(now)) {
        char id[cfg::ID_BUF_LEN];
        strncpy(id, exit_.id(), sizeof(id));
        const bool manual = exit_.manual();
        exit_.close();
        updateOutputs();
        exit_.setMessage("warn",
                         "Keine Durchfahrt innerhalb von 30 s – Ausfahrt geschlossen, "
                         "Fahrzeug bleibt im Parkhaus");
        // Nur fürs Protokoll; schlägt die Meldung fehl, ändert sich am Register nichts.
        char rid[RID_LEN];
        newRid(rid, sizeof(rid));
        kasse_.abortExit(id, manual, rid);
    }
}

}  // namespace park
