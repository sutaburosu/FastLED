#pragma once

#include "fl/audio/audio_processor.h"
#include "fl/audio/input.h"
#include "fl/stl/shared_ptr.h"

namespace fl {

class UIAudio;  // forward declaration

namespace audio {

class AudioManager {
  public:
    static AudioManager &instance();

    shared_ptr<Processor> add(const Config &config);
    shared_ptr<Processor> add(shared_ptr<IInput> input);
    shared_ptr<Processor> add(UIAudio &uiAudio);
    void remove(shared_ptr<Processor> processor);

    shared_ptr<Processor> &processor();

    AudioManager() = default;
    ~AudioManager() = default;

  private:
    AudioManager(const AudioManager &) = delete;
    AudioManager &operator=(const AudioManager &) = delete;
};

} // namespace audio
} // namespace fl
