#pragma once
// Physische Schranke. Auf dem Pico: ServoBarrier (src/schranken/PicoOutputs.h),
// im Test: BarrierMock.

namespace park {

class Barrier {
public:
    virtual ~Barrier() = default;
    virtual void open() = 0;
    virtual void close() = 0;
};

}  // namespace park
