/// @file    Sailboat.ino
/// @brief   Audio-reactive sailboat lighting with perlin noise and photon particles
/// @example Sailboat.ino

// Ported from github.com/zackees/sailboat-lights (FastLED 3.6.0 era).
// Original used custom I2S audio + custom perlin noise; now uses FastLED's
// built-in audio API and inoise16().

// @filter: (mem is high)

#include <FastLED.h>

#if !SKETCH_HAS_LOTS_OF_MEMORY
void setup() {}
void loop() {}
#else

#include "fl/ui.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define NUM_LEDS 300
#define DATA_PIN 3
#define BRIGHTNESS 255
#define COLOR_ORDER BGR
#define FRAME_TIME_MS 3 // ~333 fps target

// Envelope parameters (from led_driver.cpp)
#define ATTACK_RATE 0.4f
#define DECAY_RATE -0.04f

// Photon pool
#define N_PHOTONS 1000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
CRGB leds[NUM_LEDS];
fl::UIAudio audio_ui("Audio Input");
fl::UICheckbox allWhite("All White", false);

// Circular screen map so the web simulator knows where to place LEDs
fl::ScreenMap screenMap =
    fl::ScreenMap(NUM_LEDS, 0.15f, [](int index, fl::vec2f &pt_out) {
        float angle = (2.0f * PI * index) / NUM_LEDS;
        pt_out.x = 50.0f + 48.0f * fl::cos(angle);
        pt_out.y = 50.0f + 48.0f * fl::sin(angle);
    });

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
static float mapf(float x, float in_min, float in_max, float out_min,
                  float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static float mapf_clamped(float x, float in_min, float in_max, float out_min,
                          float out_max) {
    float v = mapf(x, in_min, in_max, out_min, out_max);
    if (v < out_min) return out_min;
    if (v > out_max) return out_max;
    return v;
}

// ---------------------------------------------------------------------------
// Perlin-noise background  (from noise_circle.cpp)
// Maps LED position to a circle and uses 3-D perlin noise to generate an
// animated cyan / white pattern.
// ---------------------------------------------------------------------------
static float circle_noise_gen(uint32_t now, double theta) {
    double x = (cos(theta) + 1) * 0xafff;
    double y = (sin(theta) + 1) * 0xafff;
    uint32_t z = now * 0x000f;
    uint16_t val = inoise16(uint32_t(x), uint32_t(y), z);
    float tmp = float(val) / float(0xcfff);
    if (tmp > 1.0f) tmp = 1.0f;
    tmp *= tmp;
    tmp *= tmp;
    tmp *= 255;
    return tmp;
}

static void noise_circle_draw(uint32_t now, CRGB *dst) {
    double time_factor = double(now) / (1024.0 * 2.0);
    for (int i = 0; i < NUM_LEDS; ++i) {
        double theta = i * 2.0 * PI / NUM_LEDS - time_factor;
        uint8_t hue = 155;
        float val = circle_noise_gen(now + 1000, theta);
        if (val < 32) {
            val = 0;
        } else {
            val = mapf(val, 32, 255, 0, 255);
        }
        hsv2rgb_spectrum(CHSV(hue, 255 - uint8_t(val), uint8_t(val)), dst[i]);
    }
}

// ---------------------------------------------------------------------------
// Audio envelope  (from led_driver.cpp)
// Converts a 0-1 audio level into a smooth 0-255 LED brightness using
// exponential attack / decay curves.
// ---------------------------------------------------------------------------
static float s_prev_vol = 0.f;

static uint8_t envelope_update(float vol, uint32_t duration_ms) {
    float time = float(duration_ms) / float(FRAME_TIME_MS);
    float next_vol;
    if (vol < s_prev_vol) {
        float dr = DECAY_RATE;
        if (s_prev_vol < 0.2f) {
            dr *= mapf_clamped(s_prev_vol, 0.2f, 0.f, 1.f, 0.f);
        }
        next_vol = s_prev_vol * expf(dr * time);
    } else {
        float maybe = s_prev_vol * expf(ATTACK_RATE * time);
        if (maybe > s_prev_vol && maybe > 0.01f) {
            // clamp between prev and candidate
            next_vol = vol < maybe ? (vol > s_prev_vol ? vol : s_prev_vol) : maybe;
        } else {
            next_vol = vol;
        }
    }
    s_prev_vol = next_vol;
    return uint8_t(next_vol * 255);
}

// ---------------------------------------------------------------------------
// Photon particle system  (from photon.cpp)
// Particles are spawned on rising audio edges and travel along the strip,
// fading out over time.
// ---------------------------------------------------------------------------
struct Photon {
    bool alive = false;
    float velocity = 0;
    float position = 0;
    float brightness = 0;
};

static Photon g_photons[N_PHOTONS];

static Photon *try_allocate_photon() {
    for (int i = 0; i < N_PHOTONS; ++i) {
        if (!g_photons[i].alive) {
            g_photons[i] = Photon();
            g_photons[i].alive = true;
            return &g_photons[i];
        }
    }
    return nullptr;
}

static uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
    return max(max(a, b), c);
}

static void photon_draw(CRGB *dst, uint8_t led_value) {
    static uint8_t s_last = 0;
    const bool is_rising = led_value > s_last;
    s_last = led_value;

    // Spawn a photon on rising audio edges
    if (led_value > 16 && is_rising) {
        Photon *p = try_allocate_photon();
        if (p) {
            uint8_t pv = max((uint8_t)(led_value / 2), (uint8_t)40);
            p->velocity = float(random(pv, pv * 2)) / 80.0f;
            p->position = 0;
            p->brightness = led_value;
        }
    }

    // Update and draw all photons
    for (int i = 0; i < N_PHOTONS; ++i) {
        Photon *p = &g_photons[i];
        if (!p->alive) continue;

        float prev_pos = p->position;
        p->position += p->velocity;
        p->velocity *= 0.999f;
        p->brightness *= 0.99f;

        if (p->position > NUM_LEDS || p->brightness < 16) {
            p->alive = false;
            continue;
        }

        CHSV tmp(160, 0, uint8_t(p->brightness));
        int idx_start = int(prev_pos);
        int idx_end = int(p->position);

        for (int j = idx_start; j <= idx_end; ++j) {
            if (j >= 0 && j < NUM_LEDS) {
                uint8_t existing = max3(dst[j].r, dst[j].g, dst[j].b);
                if (tmp.v > existing) {
                    CRGB rgb;
                    hsv2rgb_spectrum(tmp, rgb);
                    dst[j] = rgb;
                } else {
                    CRGB rgb;
                    hsv2rgb_spectrum(tmp, rgb);
                    rgb.nscale8_video(existing);
                    dst[j] += rgb;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Arduino setup / loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip)
        .setScreenMap(screenMap);
    FastLED.setBrightness(BRIGHTNESS);

    for (int i = 0; i < N_PHOTONS; ++i) {
        g_photons[i].alive = false;
    }
}

void loop() {
    uint32_t now = millis();
    uint32_t frame_end = now + FRAME_TIME_MS;

    // 1. Draw perlin noise background
    noise_circle_draw(now, leds);

    // 2. Read audio level (self-normalizing, ~1.0 = average)
    auto audio = audio_ui.processor();
    if (!audio) {
        EVERY_N_SECONDS(2) {
            Serial.println("[AUDIO] processor() returned null!");
        }
    }

    // Debug: check if raw samples are arriving
    EVERY_N_SECONDS(1) {
        bool has = audio_ui.hasNext();
        Serial.print("[AUDIO] hasNext=");
        Serial.println(has ? "true" : "false");
    }

    float bass = audio ? audio->getVibeBass() : 0.0f;
    float vol = mapf_clamped(bass, 0.3f, 2.0f, 0.0f, 1.0f);

    // Debug: print audio values periodically
    EVERY_N_SECONDS(1) {
        Serial.print("[AUDIO] bass=");
        Serial.print(bass, 4);
        Serial.print(" vol=");
        Serial.println(vol, 4);
    }

    // 3. Run through attack/decay envelope
    uint8_t led_value = envelope_update(vol, FRAME_TIME_MS);

    // Re-run envelope a few times for smoother response (matches original)
    for (int i = 0; i < 9; ++i) {
        led_value = envelope_update(vol, FRAME_TIME_MS);
    }

    // Debug: print envelope output
    EVERY_N_SECONDS(1) {
        Serial.print("[AUDIO] led_value=");
        Serial.println(led_value);
    }

    // 4. Draw photon particles driven by audio envelope
    photon_draw(leds, led_value);

    // 5. All-white override for testing
    if (allWhite) {
        fill_solid(leds, NUM_LEDS, CRGB::White);
    }

    // 6. Push to LEDs
    FastLED.show();

    // 7. Frame timing
    while (millis() < frame_end) {
        delay(1);
    }
}

#endif // SKETCH_HAS_LOTS_OF_MEMORY
