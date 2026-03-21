#pragma once

// IWYU pragma: private

#include "platforms/wasm/is_wasm.h"
#ifdef FL_IS_WASM

#include "fl/stl/string.h"
#include "fl/stl/url.h"
#include "fl/audio/audio.h"
#include "fl/audio/audio_input.h"  // For Config
#include "fl/stl/shared_ptr.h"

namespace fl {

class WasmAudioInput;
class JsonUiAudioInternal;

/**
 * WASM-specific UIAudio implementation
 * Directly wraps WasmAudioInput to read from JavaScript-provided audio samples
 */
class WasmAudioImpl {
  public:
    WasmAudioImpl(const fl::string& name);
    WasmAudioImpl(const fl::string& name, const fl::url& url);
    WasmAudioImpl(const fl::string& name, const fl::audio::Config& config);
    ~WasmAudioImpl();

    audio::Sample next();
    bool hasNext();

    // Group setting (used by JSON UI system)
    void setGroup(const fl::string& groupName);

    // Expose underlying audio input for FastLED.add() auto-pump
    fl::shared_ptr<audio::IInput> audioInput() { return mWasmInputOwner; }

  private:
    fl::string mName;
    WasmAudioInput* mWasmInput;
    fl::shared_ptr<audio::IInput> mWasmInputOwner;  // Prevent premature destruction
    bool mOwnsInput;  // Track if we created the input
    fl::shared_ptr<JsonUiAudioInternal> mInternal;  // UI registration component
};

} // namespace fl

#endif // FL_IS_WASM
