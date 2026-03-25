/// @file    ElPanelReactive.ino
/// @brief   Audio-reactive EL panel driver with adaptive signal windowing.
/// @example ElPanelReactive.ino

// Two EL panels on D2 and D1 driven at 50 Hz PWM.
// Adaptive dB windowing (songstone-style) prevents saturation:
//   - Tracks a rolling history of bass energy
//   - Computes percentile-based min/max window that adapts to volume
//   - Maps current level through the window → 0.0–1.0
// Panel 1 (D2) responds to stronger signal (squared).
// Panel 2 (D1) more responsive (linear signal).

// @filter: (mem is high)

#include <FastLED.h>
#include "el_panel.h"
#include "fl/math/filter/filter.h"
#include "fl/math/math.h"
#include "fl/ui.h"

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
fl::UIAudio audio_ui("Audio Input");
fl::UISlider sensitivity("Sensitivity", 1.5f, 0.3f, 4.0f, 0.1f);

// Panel 1 controls
fl::UISlider threshold1("Threshold", 0.2f, 0.0f, 1.0f, 0.01f);
fl::UISlider attack1("Attack", 0.08f, 0.001f, 0.5f, 0.005f);
fl::UISlider decay1("Decay", 0.3f, 0.01f, 1.0f, 0.01f);
fl::UIGroup group1("Panel 1", threshold1, attack1, decay1);

// Panel 2 controls
fl::UISlider threshold2("Threshold", 0.2f, 0.0f, 1.0f, 0.01f);
fl::UISlider attack2("Attack", 0.08f, 0.001f, 0.5f, 0.005f);
fl::UISlider decay2("Decay", 0.3f, 0.01f, 1.0f, 0.01f);
fl::UIGroup group2("Panel 2", threshold2, attack2, decay2);

// ---------------------------------------------------------------------------
// Attack/Decay filters
// ---------------------------------------------------------------------------
static fl::AttackDecayFilter<float> filterHigh(0.08f, 0.3f);
static fl::AttackDecayFilter<float> filterLow(0.08f, 0.3f);

// ---------------------------------------------------------------------------
// Adaptive bass window (ported from songstone led_controller)
// ---------------------------------------------------------------------------
#define BASS_HISTORY_SIZE 32

static float bassHistory[BASS_HISTORY_SIZE];
static int   bassHistoryCount = 0;
static int   bassHistoryIdx   = 0;

// Smoothed window bounds (low-pass filtered)
static float smoothMin = 0.0f;
static float smoothMax = 1.0f;

static void insertionSort(float* arr, int n) {
    for (int i = 1; i < n; i++) {
        float key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

// Push a new bass sample and recompute the adaptive window.
// Returns the current signal mapped to 0.0–1.0 through the window.
static float updateAdaptiveWindow(float bassRaw) {
    // Push into circular buffer
    bassHistory[bassHistoryIdx] = bassRaw;
    bassHistoryIdx = (bassHistoryIdx + 1) % BASS_HISTORY_SIZE;
    if (bassHistoryCount < BASS_HISTORY_SIZE)
        bassHistoryCount++;

    // Need minimum history before adapting
    if (bassHistoryCount < 8) {
        // Fall back to simple ratio until we have enough data
        return (smoothMax > smoothMin)
            ? fl::clamp((bassRaw - smoothMin) / (smoothMax - smoothMin), 0.0f, 1.0f)
            : 0.0f;
    }

    // Sort a copy to compute percentiles
    float sorted[BASS_HISTORY_SIZE];
    memcpy(sorted, bassHistory, bassHistoryCount * sizeof(float));
    insertionSort(sorted, bassHistoryCount);

    float q1   = sorted[bassHistoryCount / 4];         // 25th percentile
    float q7_8 = sorted[7 * bassHistoryCount / 8];     // 87.5th percentile

    // Adaptive window: quiet floor → loud ceiling
    float targetMin = q1 * 0.8f;
    float targetMax = (targetMin + 0.05f > q7_8 * 1.2f) ? targetMin + 0.05f : q7_8 * 1.2f;

    // Smooth the window — slow tracking avoids jitter
    const float kSmooth = 0.08f;
    smoothMin += kSmooth * (targetMin - smoothMin);
    smoothMax += kSmooth * (targetMax - smoothMax);

    // Map current sample through the adaptive window
    float range = smoothMax - smoothMin;
    if (range < 0.001f) return 0.0f;
    return fl::clamp((bassRaw - smoothMin) / range, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool     isSilent   = false;
static uint32_t lastMillis = 0;

void setup() {
    Serial.begin(115200);
    initPanels();

    auto audio = FastLED.add(audio_ui);
    if (audio) {
        audio->onSilence([&](u8 silent) {
            isSilent = (silent != 0);
            if (isSilent) {
                filterHigh.reset();
                filterLow.reset();
            }
        });

        audio->onVibeLevels(
            [&](const fl::audio::detector::VibeLevels &levels) {
                if (isSilent) return;

                // Map bass energy through the adaptive window
                float signal = updateAdaptiveWindow(levels.bassRaw);
                signal *= sensitivity.value();
                if (signal > 1.0f) signal = 1.0f;

                uint32_t now = millis();
                float dt = (now - lastMillis) / 1000.0f;
                lastMillis = now;

                // Apply UI-driven attack/decay settings
                filterHigh.setAttackTau(attack1.value());
                filterHigh.setDecayTau(decay1.value());
                filterLow.setAttackTau(attack2.value());
                filterLow.setDecayTau(decay2.value());

                // Panel 1: stronger signal (squared for contrast)
                filterHigh.update(signal * signal, dt);
                // Panel 2: more responsive (linear)
                filterLow.update(signal, dt);
            });
    }
}

void loop() {
    float left = filterHigh.value();
    float right = filterLow.value();
    // Remap through threshold: [threshold..1] → [0..1], below threshold → 0
    float t1 = threshold1.value();
    float t2 = threshold2.value();
    left  = fl::clamp((left  - t1) / (1.0f - t1 + 1e-6f), 0.0f, 1.0f);
    right = fl::clamp((right - t2) / (1.0f - t2 + 1e-6f), 0.0f, 1.0f);
    setPanelHigh(left);
    setPanelLow(right);
    showPanels();
}
