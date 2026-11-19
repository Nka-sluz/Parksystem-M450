#pragma once
// Zeitquelle für die Logik: Millisekunden seit Start.
//
// Auf dem Pico liefert PicoClock (include/PicoClock.h) millis(), im Test eine
// FakeClock, die der Test von Hand vorstellt. So lassen sich Parkdauern,
// Karenzzeit und der 30-s-Timeout der Schranke ohne Warten prüfen.

#include <stdint.h>

namespace park {

class Clock {
public:
    virtual ~Clock() = default;
    virtual uint32_t nowMs() const = 0;
};

}  // namespace park
