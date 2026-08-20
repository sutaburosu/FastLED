// FL_AGENT_ALLOW_NEW_EXAMPLE
/// @file    Fireworks3D.ino
/// @brief   3D fireworks scene (roman candles + catherine wheel) on a 128x64 matrix
/// @example Fireworks3D.ino
///
/// A small 3D fireworks display: roman-candle rockets ascend trailing sparks
/// and burst into spherical shells, while a catherine wheel on a stand spins
/// up and ejects a spiral of white-hot sparks. The scene is simulated in
/// world space, perspective-projected, and drawn with the fl::gfx primitives
/// (drawDisc / drawLine / drawRing) with a fading-frame trail.
///
/// Matrix: 128x64, wired LINE-BY-LINE (not serpentine) — row 0 to row 63,
/// each row left to right. The render buffer is plain row-major, which
/// matches that physical layout exactly.
///
/// Hardware: the production display is a 128x64 HUB75 SmartMatrix panel.
/// This sketch drives the same buffer through the NEOPIXEL chipset so it
/// compiles and runs in the WASM browser build; to drive a real panel, hand
/// the rendered `leds` buffer to a HUB75 driver (e.g. the SmartMatrix
/// library) instead of calling FastLED.show().
///
/// See Fireworks3D.h for the scene engine.

// @filter: (memory is huge)

#include "FastLED.h"
#include <Arduino.h>
#include "Fireworks3D.h"

// ---- Hardware / layout configuration ---------------------------------------
#define DATA_PIN   2   // LED data pin — change to match your wiring
#define BRIGHTNESS 96
#define WIDTH      128
#define HEIGHT     64
#define NUM_LEDS   (WIDTH * HEIGHT)

// The matrix is wired line-by-line (NOT serpentine). The render buffer is
// already in that order, so the screen map is the identity — set explicitly
// to document the layout.
CRGB leds[NUM_LEDS];
fl::XYMap xyMap(WIDTH, HEIGHT, /*is_serpentine=*/false);

fireworks::Scene gScene;

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