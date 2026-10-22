#pragma once
// Gebührenberechnung (US3).
//
//  - Parkdauer < Freigrenze               → 0
//  - sonst: angefangene Zeiteinheiten × Preis pro Einheit
//  - pro angefangenem Tag höchstens der Tageshöchstsatz
//
// Alle Tarifwerte stammen aus include/Config.h.

#include <stdint.h>

#include "Config.h"

namespace park {

struct Tariff {
    uint32_t unitMs;
    cfg::Money pricePerUnit;
    uint32_t freeMs;
    cfg::Money dailyMax;
    uint32_t dayMs;

    static constexpr Tariff fromConfig() {
        return Tariff{cfg::TARIFF_UNIT_MS, cfg::TARIFF_PRICE_PER_UNIT, cfg::TARIFF_FREE_MS,
                      cfg::TARIFF_DAILY_MAX, cfg::TARIFF_DAY_MS};
    }

    cfg::Money feeFor(uint32_t durationMs) const;
};

}  // namespace park
