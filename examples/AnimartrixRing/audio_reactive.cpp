// audio_reactive.cpp
#include "audio_reactive.h"
#include "fl/audio/detectors/vibe.h"

void AudioReactive::begin(fl::UIAudio &audio) {
    auto input = audio.audioInput();
    if (input) {
        processor = FastLED.add(input);
        autoPump = true;
        printf("AnimartrixRing: Audio routed via FastLED.add() (auto-pump)\n");
    }
    if (!processor) {
        processor = fl::make_shared<fl::AudioProcessor>();
        printf("AnimartrixRing: Audio using manual pump (fallback)\n");
    }
}

void AudioReactive::connectToEngine(fl::FxEngine &fxEngine,
                                    fl::UICheckbox &enableVibe,
                                    fl::UISlider &speedMultiplier,
                                    fl::UISlider &baseSpeed,
                                    fl::UISlider &timeSpeed) {
    processor->onVibeLevels(
        [&fxEngine, &enableVibe, &speedMultiplier, &baseSpeed,
         &timeSpeed](const fl::VibeLevels &vibe) {
            if (!enableVibe.value())
                return;
            printf("Vibe: bass=%.2f mid=%.2f treb=%.2f | spikes: "
                   "bass=%d mid=%d treb=%d\n",
                   vibe.bass, vibe.mid, vibe.treb, vibe.bassSpike,
                   vibe.midSpike, vibe.trebSpike);
            float bassBoost = (vibe.bass - 1.0f) * speedMultiplier.value();
            float speed = baseSpeed.value() + bassBoost;
            speed *= timeSpeed.value();
            fxEngine.setSpeed(speed);
        });

    processor->onVibeBassSpike([]() { printf(">>> BASS SPIKE!\n"); });
    processor->onVibeMidSpike([]() { printf(">>> MID SPIKE!\n"); });
    processor->onVibeTrebSpike([]() { printf(">>> TREB SPIKE!\n"); });
}

void AudioReactive::pump(fl::UIAudio &audio, fl::UICheckbox &enableVibe) {
    if (autoPump)
        return;
    fl::AudioSample sample = audio.next();
    if (!sample.isValid())
        return;
    sampleCount++;
    if (sampleCount == 1) {
        printf("AnimartrixRing: First audio sample received! "
               "enableVibeReactive=%d\n",
               (int)enableVibe.value());
    } else if (sampleCount % 172 == 0) {
        printf("AnimartrixRing: %u audio samples processed, "
               "enableVibeReactive=%d\n",
               (unsigned)sampleCount, (int)enableVibe.value());
    }
    if (enableVibe.value()) {
        processor->update(sample);
    }
}
