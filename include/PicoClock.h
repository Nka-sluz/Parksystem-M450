#pragma once
// Echte Zeitquelle auf dem Pico: Millisekunden seit Start (millis()).

#include <Arduino.h>

#include "Clock.h"

class PicoClock : public park::Clock {
public:
    uint32_t nowMs() const override { return millis(); }
};
