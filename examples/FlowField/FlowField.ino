// @filter: (memory is high)

/// @file    FlowField.ino
/// @brief   2D flow field visualization: emitters paint color, noise advects it
/// @example FlowField.ino
///
/// Concept by Stefan Petrick. Emitters inject color onto a float-precision 2D
/// grid, then a noise-driven flow field advects (transports) those colors with
/// bilinear interpolation, creating emergent fluid-like patterns.
///
/// Original C++ implementation by 4wheeljive (ColorTrails project).
/// Distilled into a self-contained FastLED example.
///
/// This sketch is fully compatible with the FastLED web compiler. To use it:
/// 1. Install FastLED: `pip install fastled`
/// 2. cd into this examples directory
/// 3. Run: `fastled`
/// 4. A browser window will open with the preview.

// UIDescription: Flow field visualization with noise-driven advection, creating
// fluid-like patterns from color emitters.

#include <FastLED.h>
#include "fl/ui.h"
#include "fl/fx/2d/flowfield.h"

// -- Matrix config --
#define WIDTH 64
#define HEIGHT 64
#define NUM_LEDS (WIDTH * HEIGHT)
#define DATA_PIN 2
#define BRIGHTNESS 255
#define SERPENTINE true

CRGB leds[NUM_LEDS];
fl::XYMap xyMap(WIDTH, HEIGHT, SERPENTINE);

// -- UI controls --
fl::UISlider flowSpeedX("X Speed", 0.10f, -2.0f, 2.0f, 0.01f);
fl::UISlider flowAmpX("X Amplitude", 1.0f, 0.0f, 2.0f, 0.01f);
fl::UISlider flowFreqX("X Frequency", 0.33f, 0.05f, 4.0f, 0.01f);
fl::UISlider flowSpeedY("Y Speed", 0.10f, -2.0f, 2.0f, 0.01f);
fl::UISlider flowAmpY("Y Amplitude", 1.0f, 0.0f, 2.0f, 0.01f);
fl::UISlider flowFreqY("Y Frequency", 0.32f, 0.05f, 4.0f, 0.01f);
fl::UISlider endpointSpeed("Endpoint Speed", 0.80f, 0.05f, 2.0f, 0.01f);
fl::UISlider colorShift("Color Shift", 0.04f, 0.0f, 0.5f, 0.01f);
fl::UISlider persistence("Trail Half-Life (s)", 0.86f, 0.05f, 5.0f, 0.01f);
fl::UISlider flowShift("Pixel Shift", 1.8f, 0.5f, 4.0f, 0.1f);
fl::UISlider numDots("Dots", 3, 1, 5, 1);
fl::UIDropdown emitterMode("Emitter Mode", {"Lissajous", "Dots", "Both"});

fl::FlowField flowField(xyMap);

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS)
        .setScreenMap(xyMap);
    FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
    // Push UI slider values into the effect.
    flowField.setFlowSpeedX(flowSpeedX);
    flowField.setFlowAmplitudeX(flowAmpX);
    flowField.setNoiseFrequencyX(flowFreqX);
    flowField.setFlowSpeedY(flowSpeedY);
    flowField.setFlowAmplitudeY(flowAmpY);
    flowField.setNoiseFrequencyY(flowFreqY);
    flowField.setEndpointSpeed(endpointSpeed);
    flowField.setColorShift(colorShift);
    flowField.setPersistence(persistence);
    flowField.setFlowShift(flowShift);
    flowField.setDotCount(numDots.as<int>());
    flowField.setEmitterMode(emitterMode.as_int());

    fl::Fx::DrawContext ctx(millis(), leds);
    flowField.draw(ctx);

    FastLED.show();
}
