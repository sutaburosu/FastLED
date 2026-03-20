#pragma once

// IWYU pragma: private

#include "platforms/esp/32/audio/devices/i2s.hpp"
#include "platforms/audio_input_null.hpp"

// Ensure FASTLED_ESP32_I2S_SUPPORTED is defined (included via i2s.hpp)
#ifndef FASTLED_ESP32_I2S_SUPPORTED
#error "FASTLED_ESP32_I2S_SUPPORTED should be defined by including i2s.hpp"
#endif

namespace fl {

// ESP32-specific audio input creation function
fl::shared_ptr<audio::IInput> esp32_create_audio_input(const audio::Config &config, fl::string *error_message) {
    if (config.is<audio::ConfigI2S>()) {
#if FASTLED_ESP32_I2S_SUPPORTED
        FL_WARN("Creating I2S standard mode audio source");
        audio::ConfigI2S std_config = config.get<audio::ConfigI2S>();
        fl::shared_ptr<audio::IInput> out = fl::make_shared<fl::I2S_Audio>(std_config);
        return out;
#else
        const char* ERROR_MESSAGE = "I2S audio not supported on this ESP32 variant (no I2S hardware)";
        FL_WARN(ERROR_MESSAGE);
        if (error_message) {
            *error_message = ERROR_MESSAGE;
        }
        return fl::make_shared<fl::Null_Audio>();
#endif
    }
    const char* ERROR_MESSAGE = "Unsupported audio configuration";
    FL_WARN(ERROR_MESSAGE);
    if (error_message) {
        *error_message = ERROR_MESSAGE;
    }
    return fl::make_shared<fl::Null_Audio>();
}

} // namespace fl
