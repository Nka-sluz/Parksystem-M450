#pragma once
// Prüfung und Normalisierung von Kennzeichen bzw. Ersatz-Identifikatoren.
//
// Gültiges Kennzeichen (nach Entfernen von Leerzeichen am Rand, in Grossbuchstaben):
//   - 2 bis cfg::PLATE_MAX_LEN Zeichen
//   - nur A–Z, 0–9 und '-'
//   - beginnt mit einem Buchstaben, endet nicht mit '-', enthält mind. eine Ziffer
//   Beispiele: "ZH-100001", "BE12345", "XX-999999"
//
// Ersatz-Identifikator (US8): "MANUAL-" gefolgt von 1–5 Ziffern, z. B. "MANUAL-1".
// Er ist für die manuelle Abfertigung reserviert und an der automatischen
// Einfahrt nicht als Kennzeichen zulässig.

#include <stddef.h>

#include "Config.h"

namespace park {

enum class PlateCheck : unsigned char {
    Ok,
    Empty,
    TooLong,
    BadFormat,
    Reserved,  // Ersatz-Identifikator dort, wo nur ein echtes Kennzeichen erlaubt ist
};

// Prüft `raw` und schreibt bei Erfolg die normalisierte Form nach `out`
// (Grösse mind. cfg::ID_BUF_LEN). `allowManualId` erlaubt "MANUAL-<n>".
PlateCheck normalizeId(const char* raw, char* out, size_t outLen, bool allowManualId);

bool isManualId(const char* normalized);

// Macht beliebige Eingaben protokolltauglich: kürzt, ersetzt Steuer- und
// Trennzeichen (';', '"', '\\', Nicht-ASCII) durch '?'. Ergebnis nie leer
// ("(leer)" für leere Eingaben).
void sanitizeForLog(const char* raw, char* out, size_t outLen);

}  // namespace park
