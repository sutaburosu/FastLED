/// @file    Sailboat.ino
/// @brief   Audio-reactive sailboat lighting with perlin noise and photon particles
/// @example Sailboat.ino

// Ported from github.com/zackees/sailboat-lights (FastLED 3.6.0 era).
// The effect logic lives in PerlinParticlePunch (Fx1d); this sketch wires up
// hardware, audio input, and the screen map.

// @filter: (mem is high)

#include <FastLED.h>

#if !SKETCH_HAS_LOTS_OF_MEMORY
void setup() {}
void loop() {}
#else

#include "fl/fx/1d/perlin_particle_punch.h"
#include "fl/ui.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define NUM_LEDS 300
#define DATA_PIN 3
#define BRIGHTNESS 255
#define COLOR_ORDER BGR

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
CRGB leds[NUM_LEDS];
fl::UIAudio audio_ui("Audio Input");
fl::UICheckbox allWhite("All White", false);
fl::UISlider dragSlider("Particle Drag", 0.05f, 0.0f, 1.0f, 0.01f);
fl::UISlider speedSlider("Particle Speed", 0.5f, 0.1f, 3.0f, 0.1f);
fl::PerlinParticlePunch sailboatFx(NUM_LEDS);

// Screen map: LEDs follow the main sail hypotenuse with natural sag (catenary)
fl::ScreenMap screenMap =
    fl::ScreenMap(NUM_LEDS, 0.15f, [](int index, fl::vec2f &pt_out) {
        float t = float(index) / float(NUM_LEDS - 1);
        // Straight line: lower-left (15, 5) → upper-right (95, 95)
        // Y-up coordinate system: high y = top of screen
        pt_out.x = 15.0f + t * 80.0f;
        float straight_y = 5.0f + t * 90.0f;
        // Sag: catenary droop, subtract to bulge downward visually
        float sag = 15.0f * fl::sin(t * PI);
        pt_out.y = straight_y - sag;
    });

// ---------------------------------------------------------------------------
// Arduino setup / loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip)
        .setScreenMap(screenMap);
    FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
    // 1. Read audio level (self-normalizing, ~1.0 = average)
    auto audio = audio_ui.processor();
    float bass = audio ? audio->getVibeBass() : 0.0f;
    float vol = fl::clamp(
        (bass - 0.3f) / (2.0f - 0.3f), // mapf_clamped(bass, 0.3, 2.0, 0, 1)
        0.0f, 1.0f);

    // 2. Feed audio and drag to effect, then draw
    sailboatFx.setAudioLevel(vol);
    // Slider 0..1 maps to internal drag factor 0.99..0.80
    // (0 = no drag, 1 = max drag)
    float drag = 0.99f - dragSlider.value() * 0.19f;
    sailboatFx.setDrag(drag);
    sailboatFx.setSpeed(speedSlider.value());
    fl::Fx::DrawContext ctx(millis(), leds);
    sailboatFx.draw(ctx);

    // 3. All-white override for testing
    if (allWhite) {
        fill_solid(leds, NUM_LEDS, CRGB::White);
    }

    // 4. Push to LEDs
    FastLED.show();
}

#endif // SKETCH_HAS_LOTS_OF_MEMORY
