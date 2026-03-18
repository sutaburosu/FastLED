// @filter: (memory is high)

// AnimartrixRing: Sample a circle from an Animartrix rectangular grid
// Uses AudioProcessor's VibeDetector for self-normalizing bass/mid/treb
// levels. Bass level warps animation speed via FxEngine's TimeWarp.

// Use SPI-based WS2812 driver instead of RMT on ESP32
#define FASTLED_ESP32_USE_CLOCKLESS_SPI

// FastLED.h must be included first to trigger precompiled headers for FastLED's
// build system
#include "FastLED.h"

#include "fl/ui.h"
#include "fl/fx/2d/animartrix.hpp"
#include "fl/fx/fx2d_to_1d.h"
#include "fl/fx/fx_engine.h"
#include <FastLED.h>

#include "ring_screenmap.h"
#include "auto_brightness.h"
#include "audio_reactive.h"

FASTLED_TITLE("AnimartrixRing");

#define NUM_LEDS 244

#ifndef PIN_DATA
#define PIN_DATA 3
#endif

#define BRIGHTNESS 8
#define GRID_WIDTH 16
#define GRID_HEIGHT 16

CRGB leds[NUM_LEDS];

// Animartrix 2D effect
XYMap xymap = XYMap::constructRectangularGrid(GRID_WIDTH, GRID_HEIGHT);
auto animartrix = fl::make_shared<fl::Animartrix>(xymap, fl::RGB_BLOBS5);

// Circular sampling from the rectangular grid
fl::ScreenMap screenmap = makeRingScreenMap(NUM_LEDS, GRID_WIDTH, GRID_HEIGHT);

// 2D-to-1D sampling effect + engine
auto fx2dTo1d = fl::make_shared<fl::Fx2dTo1d>(
    NUM_LEDS, animartrix, screenmap,
    fl::Fx2dTo1d::BILINEAR);

fl::FxEngine fxEngine(NUM_LEDS);

// Animation selector helpers
fl::vector<fl::string> getAnimationNames() {
    fl::vector<fl::pair<int, fl::string>> animList =
        fl::Animartrix::getAnimationList();
    fl::vector<fl::string> names;
    for (const auto &item : animList) {
        names.push_back(item.second);
    }
    return names;
}
static fl::vector<fl::string> animationNames = getAnimationNames();

// UI controls
fl::UIDropdown animationSelector("Animation", animationNames);
fl::UISlider timeSpeed("Time Speed", 1, -10, 10, .1);
fl::UISlider brightness("Brightness", BRIGHTNESS, 0, 255, 1);
fl::UICheckbox autoBrightness("Auto Brightness", true);
fl::UISlider autoBrightnessMax("Auto Brightness Max", 84, 0, 255, 1);
fl::UISlider autoBrightnessLowThreshold("Auto Brightness Low Threshold", 8, 0,
                                        100, 1);
fl::UISlider autoBrightnessHighThreshold("Auto Brightness High Threshold", 22,
                                         0, 100, 1);

// Audio UI controls
fl::UIAudio audio("Audio Input");
fl::UICheckbox enableVibeReactive("Enable Vibe Reactive", false);
fl::UISlider vibeSpeedMultiplier("Vibe Speed Multiplier", 3.0, 0.0, 10.0, 0.1);
fl::UISlider vibeBaseSpeed("Vibe Base Speed", 1.0, 0.0, 5.0, 0.1);

// Audio-reactive engine
AudioReactive audioReactive;

void setup() {
    Serial.begin(115200);

    fl::ScreenMap screenMapLocal(screenmap);
    screenMapLocal.setDiameter(0.15);
    FastLED.addLeds<WS2812, PIN_DATA>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip)
        .setScreenMap(screenMapLocal);
    FastLED.setBrightness(brightness.value());

    fxEngine.addFx(fx2dTo1d);

    animationSelector.onChanged([](fl::UIDropdown &dropdown) {
        animartrix->fxSet(dropdown.as_int());
    });

    audioReactive.begin(audio);
    audioReactive.connectToEngine(fxEngine, enableVibeReactive,
                                  vibeSpeedMultiplier, vibeBaseSpeed, timeSpeed);

    Serial.println("AnimartrixRing setup complete");
}

void loop() {
    const uint32_t now = millis();
    audioReactive.pump(audio, enableVibeReactive);

    if (!enableVibeReactive.value()) {
        fxEngine.setSpeed(timeSpeed.value());
    }

    fxEngine.draw(now, leds);

    uint8_t finalBrightness;
    if (autoBrightness.value()) {
        float avgBri = getAverageBrightness(leds, NUM_LEDS);
        finalBrightness = applyBrightnessCompression(
            avgBri, static_cast<uint8_t>(autoBrightnessMax.value()),
            autoBrightnessLowThreshold.value(),
            autoBrightnessHighThreshold.value());
    } else {
        finalBrightness = static_cast<uint8_t>(brightness.value());
    }

    FastLED.setBrightness(finalBrightness);
    FastLED.show();
}
