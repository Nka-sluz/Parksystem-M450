#pragma once
// Anzeige "Schranke offen". Auf dem Pico: Onboard-LED (LedLamp in
// src/schranken/PicoOutputs.h), im Test: LampSpy bzw. LampDummy.

namespace park {

class Lamp {
public:
    virtual ~Lamp() = default;
    virtual void set(bool on) = 0;
};

}  // namespace park
