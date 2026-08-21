// FL_AGENT_ALLOW_NEW_EXAMPLE
/// @file    Pyrotechny.ino
/// @brief   3D pyrotechnic scene (fountains + catherine wheel) on a 128x64 matrix
/// @example Pyrotechny.ino
///
/// A slowly rotating 3D pyrotechnic display built on one shared particle
/// system: three ground-level fountains spew dense sprays of tiny sparks in
/// their own colours (most of them cool before they fall back), while a
/// catherine wheel on a stand spins up under rocket thrust and sheds a
/// spiral of white-hot sparks that fly off faster as it spins faster. The
/// scene is simulated in world space, perspective-projected, and drawn with
/// the fl::gfx primitives (graded multi-segment trails + fading-frame
/// afterglow).
///
/// Matrix: 128x64, wired LINE-BY-LINE (not serpentine) - row 0 to row 63,
/// each row left to right. The render buffer is plain row-major, which
/// matches that physical layout exactly.
///
/// Hardware: the production display is a 128x64 HUB75 SmartMatrix panel.
/// This sketch drives the same buffer through the NEOPIXEL chipset so it
/// compiles and runs in the WASM browser build; to drive a real panel, hand
/// the rendered `leds` buffer to a HUB75 driver (e.g. the SmartMatrix
/// library) instead of calling FastLED.show().
///
/// See Pyrotechny.h for the scene engine.

// @filter: (memory is huge)

#include "FastLED.h"
#include <Arduino.h>
#include "Pyrotechny.h"

// ---- Hardware / layout configuration ---------------------------------------
#define DATA_PIN   2   // LED data pin - change to match your wiring
#define BRIGHTNESS 96
#define WIDTH      128
#define HEIGHT     64
#define NUM_LEDS   (WIDTH * HEIGHT)

// The matrix is wired line-by-line (NOT serpentine). The render buffer is
// already in that order, so the screen map is the identity - set explicitly
// to document the layout.
CRGB leds[NUM_LEDS];
fl::XYMap xyMap(WIDTH, HEIGHT, /*is_serpentine=*/false);

pyrotechny::Scene gScene;

void setup() {
    random16_set_seed(uint16_t(fl::millis()));
    fl::memset(leds, 0, sizeof(leds));
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS).setScreenMap(xyMap);
    FastLED.setBrightness(BRIGHTNESS);
    gScene.reset();
}

void loop() {
    static fl::u32 lastMs = 0;
    const fl::u32 now = fl::millis();
    float dt = float(now - lastMs) / 1000.0f;  // wrap-safe unsigned delta
    lastMs = now;
    if (dt > 0.1f)
        dt = 0.1f;  // clamp tab-switch / startup gaps
    if (dt <= 0.0f)
        dt = 1.0f / 60.0f;

    gScene.update(dt);
    gScene.render(leds);
    FastLED.show();
    FastLED.delay(16);  // ~60 fps
}