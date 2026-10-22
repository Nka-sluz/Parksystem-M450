#pragma once
// Betriebsprotokoll (US7): Ringpuffer fester Grösse.
//
// Jeder Eintrag erhält eine fortlaufende Nummer. Ist der Puffer voll, wird der
// älteste Eintrag überschrieben; erkennbar an overwrittenCount() > 0 bzw. daran,
// dass die erste angezeigte Nummer grösser als 1 ist.

#include <stddef.h>
#include <stdint.h>

#include "Config.h"

namespace park {

enum class EventType : unsigned char {
    Entry,
    EntryDenied,
    Payment,
    Exit,
    ExitDenied,
    ExitAborted,  // Ausfahrt freigegeben, aber keine Durchfahrt innerhalb des Timeouts
};

// Gründe für Ablehnungen bzw. Ergebnisse. Wird auch im HTTP-Protokoll zwischen
// den Picos verwendet (siehe reasonToken / reasonFromToken).
enum class Reason : unsigned char {
    None,
    Invalid,      // Kennzeichen ungültig
    Duplicate,    // bereits eingefahren
    Full,         // Parkhaus voll
    Unpaid,       // offener Betrag > 0
    Unknown,      // nicht im Register
    Exited,       // bereits ausgefahren
    NothingDue,   // nichts zu bezahlen
    Insufficient, // Betrag nicht ausreichend
    Timeout,      // keine Durchfahrt innerhalb des Timeouts
    NotOpen,      // Durchfahrt bestätigt, aber Schranke war nicht (mehr) offen
};

const char* eventTypeText(EventType t);  // z. B. "EINFAHRT"
const char* reasonText(Reason r);        // kurzer Grund fürs Protokoll, z. B. "ungültig"
const char* reasonMessage(Reason r);     // Meldung für die Weboberfläche
const char* reasonToken(Reason r);       // Token im HTTP-Protokoll, z. B. "INVALID"
Reason reasonFromToken(const char* token);

struct LogEntry {
    uint32_t seq;
    uint32_t timeMs;
    EventType type;
    Reason reason;
    bool manual;
    bool hasAmount;
    cfg::Money amount;
    char id[cfg::ID_BUF_LEN];
};

class EventLog {
public:
    void clear();
    void add(uint32_t nowMs, EventType type, const char* id, Reason reason = Reason::None,
             bool manual = false, bool hasAmount = false, cfg::Money amount = 0);

    size_t size() const { return count_; }
    uint32_t totalCount() const { return nextSeq_ - 1; }
    uint32_t overwrittenCount() const { return totalCount() - static_cast<uint32_t>(count_); }

    // i = 0 ist der älteste noch vorhandene Eintrag (zeitliche Reihenfolge).
    const LogEntry& at(size_t i) const;

    static constexpr const char* CSV_HEADER = "nr;zeit;ereignis;kennzeichen;betrag_chf;grund;manuell";
    // Schreibt einen Eintrag als CSV-Zeile (ohne Zeilenumbruch).
    static void formatCsv(const LogEntry& e, char* out, size_t outLen);

private:
    LogEntry entries_[cfg::LOG_CAPACITY] = {};
    size_t head_ = 0;  // Index des ältesten Eintrags
    size_t count_ = 0;
    uint32_t nextSeq_ = 1;
};

}  // namespace park
