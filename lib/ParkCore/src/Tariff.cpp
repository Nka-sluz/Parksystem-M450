#include "Tariff.h"

namespace park {

namespace {

uint32_t startedUnits(uint32_t durationMs, uint32_t unitMs) {
    return durationMs / unitMs + (durationMs % unitMs != 0 ? 1 : 0);
}

}  // namespace

cfg::Money Tariff::feeFor(uint32_t durationMs) const {
    if (durationMs < freeMs) return 0;

    const uint32_t fullDays = durationMs / dayMs;
    const uint32_t restMs = durationMs % dayMs;

    cfg::Money rest = static_cast<cfg::Money>(startedUnits(restMs, unitMs)) * pricePerUnit;
    if (rest > dailyMax) rest = dailyMax;

    return static_cast<cfg::Money>(fullDays) * dailyMax + rest;
}

}  // namespace park
