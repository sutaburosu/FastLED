#pragma once

namespace fl {
namespace audio {

class Context;

class Detector {
public:
    virtual ~Detector() = default;

    virtual void update(shared_ptr<Context> context) = 0;
    virtual void fireCallbacks() {}
    virtual void setSampleRate(int) {}
    virtual bool needsFFT() const { return false; }
    virtual bool needsFFTHistory() const { return false; }
    virtual const char* getName() const = 0;
    virtual void reset() {}
};

} // namespace audio
} // namespace fl
