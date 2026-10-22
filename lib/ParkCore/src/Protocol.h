#pragma once
// HTTP-Protokoll zwischen Pico 2 (Schranken) und Pico 1 (Kasse).
// Vollständige Beschreibung: docs/HTTP-Protokoll.md
//
// Anfragen: HTTP POST (bzw. GET für /api/status) mit Formularparametern
//           (application/x-www-form-urlencoded).
// Antworten: text/plain, eine Zeile pro Feld im Format "key=value\n".
//           Fachliche Ergebnisse (auch Ablehnungen) → HTTP 200.
//           Fehlende Parameter → HTTP 400 mit result=ERROR.

#include <stddef.h>

namespace proto {

// Endpunkte auf Pico 1
constexpr const char* PATH_ENTRY = "/api/entry";               // POST plate=… | manual=1
constexpr const char* PATH_EXIT_CHECK = "/api/exit/check";     // POST plate=… [manual=1]
constexpr const char* PATH_EXIT_CONFIRM = "/api/exit/confirm"; // POST plate=… [manual=1]
constexpr const char* PATH_EXIT_ABORT = "/api/exit/abort";     // POST plate=… [manual=1]
constexpr const char* PATH_STATUS = "/api/status";             // GET

// Parameter
constexpr const char* P_PLATE = "plate";
constexpr const char* P_MANUAL = "manual";

// Antwortfelder
constexpr const char* F_RESULT = "result";    // GRANTED | DENIED | OPEN | OK | ERROR
constexpr const char* F_REASON = "reason";    // siehe park::reasonToken()
constexpr const char* F_ID = "id";            // normalisiertes Kennzeichen / MANUAL-<n>
constexpr const char* F_DUE = "due";          // offener Betrag in Rappen
constexpr const char* F_CAPACITY = "capacity";
constexpr const char* F_OCCUPIED = "occupied";
constexpr const char* F_FREE = "free";

constexpr const char* R_GRANTED = "GRANTED";
constexpr const char* R_DENIED = "DENIED";
constexpr const char* R_OPEN = "OPEN";
constexpr const char* R_OK = "OK";
constexpr const char* R_ERROR = "ERROR";

// Liest das Feld `key` aus einer "key=value"-Antwort. Liefert false, wenn das
// Feld fehlt. Der Wert wird bei Bedarf auf outLen-1 Zeichen gekürzt.
bool getField(const char* body, const char* key, char* out, size_t outLen);

}  // namespace proto
