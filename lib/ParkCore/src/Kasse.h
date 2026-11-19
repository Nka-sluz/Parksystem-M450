#pragma once
// Verbindung von Pico 2 (Schranken) zur Kasse (Pico 1), die jede Entscheidung
// trifft. Auf dem Pico: HttpKasse (src/schranken/HttpKasse.h, HTTP über WLAN,
// siehe docs/HTTP-Protokoll.md), im Test: KasseStub bzw. KasseFake.

#include "Config.h"
#include "EventLog.h"

namespace park {

struct KasseReply {
    bool accepted;             // Einfahrt freigegeben (GRANTED) bzw. Ausfahrt offen (OPEN)
    Reason reason;             // Grund einer Ablehnung
    char id[cfg::ID_BUF_LEN];  // normalisiertes Kennzeichen bzw. MANUAL-<n>
    cfg::Money due;            // offener Betrag (Ausfahrt)
};

class Kasse {
public:
    virtual ~Kasse() = default;

    // Alle Methoden liefern false, wenn keine gültige Antwort kam (WLAN weg,
    // Kasse aus). Die Schranke bleibt dann zu (Fail-safe).
    // `rid` = Request-ID: Wiederholungen derselben Anfrage tragen dieselbe ID,
    // die Kasse führt die Aktion dann nur einmal aus.
    virtual bool requestEntry(const char* plate, const char* rid, KasseReply& reply) = 0;
    virtual bool requestManualEntry(const char* rid, KasseReply& reply) = 0;
    virtual bool checkExit(const char* id, bool manual, const char* rid, KasseReply& reply) = 0;
    // Durchfahrt an der Ausfahrt melden. Auch eine fachliche Ablehnung
    // (z. B. bereits ausgefahren) gilt als quittiert.
    virtual bool confirmExit(const char* id, bool manual, const char* rid) = 0;
    // Freigegebene Ausfahrt ohne Durchfahrt (Timeout), nur fürs Protokoll.
    virtual bool abortExit(const char* id, bool manual, const char* rid) = 0;
};

}  // namespace park
