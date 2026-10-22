#pragma once
// Formatierung von Beträgen und Zeiten (ohne dynamischen Speicher).

#include <stddef.h>
#include <stdint.h>

#include "Config.h"

namespace park {

// 1250 → "12.50"
void formatMoney(cfg::Money rappen, char* out, size_t outLen);

// Millisekunden → "HH:MM:SS" (Stunden können > 23 sein). Wird für Zeitstempel
// (Zeit seit Start der Kasse) und für Parkdauern verwendet.
void formatClock(uint32_t ms, char* out, size_t outLen);

// "12.50" / "12" / "12.5" → 1250. Liefert false bei ungültiger Eingabe.
bool parseMoney(const char* text, cfg::Money& out);

// Escaped `in` für einen JSON-String (ohne Anführungszeichen). Kürzt bei
// Platzmangel, ohne eine Escape-Sequenz zu zerschneiden.
void jsonEscape(const char* in, char* out, size_t outLen);

}  // namespace park
