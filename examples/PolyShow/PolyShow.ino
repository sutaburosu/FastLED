/// @file    PolyShow.ino
/// @brief   Antialiased drawPolygon edge-case showcase on a 16x8 canvas
/// @example PolyShow.ino
// FL_AGENT_ALLOW_NEW_EXAMPLE
//
// The canvas is a fl::CanvasRGB view directly over the LED array (no copy);
// wire a 16x8 matrix (128 WS2812 in row-major order) to PIN_DATA. Cycles
// six scenes, each exercising a different drawPolygon edge case:
//   1. Bowtie     - self-intersecting quad, even-odd fill (center pinches out)
//   2. Subpixel   - fractional-pixel vertices, antialiased edge ramps
//   3. Clamp      - vertices at +/-60..100 px, clamped to +/-8191 px, then to canvas
//   4. Sliver     - near-horizontal edges (dy < 1 px) spanning many x columns
//   5. Degenerate - 2-vertex and horizontal-only polygons (must draw nothing)
//   6. Diamond    - vertices exactly on row centers (y = k + 0.5 px)
// Also prints a 16x8 brightness preview (2 hex digits per pixel) over Serial.
//
// Integer-only: s16x16 Q16.16 coordinates, no floats. The drawPolygon tier
// (MAXN=4, CHUNK=1) keeps the working set small enough for ATmega328P
// (2 KB SRAM). Budget measured on the built firmware (avr-gcc 7.3 -Os;
// runtime depths from a cycle-accurate AVR trace of the ELF):
//
//   static image        943 B    (.data 274 + .bss 669, ends 0x4AF; 384 B
//                                 is the 16x8 LED array, vertex tables live
//                                 in flash and stage through a 32 B buffer)
//   stack available     1104 B   (SP resets to 0x8FF; 0x8FD at main entry)
//   deepest draw chain  735 B    (drawPolygonCore 564 B frame [18 pushed
//                                 registers + 0x222 scanline workspace
//                                 incl. per-row band tables], polyLineT28
//                                 64-bit helpers, UART TX ISR preempting
//                                 at the deepest point; min SP 0x61e)
//   headroom            367 B    (min SP 0x61e vs static end 0x4AF)
// A 16x16 canvas (768 B LEDs) would add 384 B of static image, leaving
// only 720 B of stack: the 735 B draw chain no longer fits. Raising
// TIER_MAXN or the canvas size erases even the remaining margin;
// re-verify the budget (avr-size/avr-nm on the firmware ELF plus an AVR
// runtime trace) before either.

// @filter: (memory is low)
// The 384 B LED/canvas buffer plus the drawPolygon<,4,1> working set and
// stack needs more than 1 KB SRAM, so sub-1 KB "tiny" boards are excluded.

#include <Arduino.h>
#include <FastLED.h>
#include <fl/gfx/gfx.h>

// Host (non-AVR) builds of this example have no flash; keep the macro
// meaningful on AVR and inert elsewhere.
#ifndef PROGMEM
#define PROGMEM
#endif

#define NUM_LEDS (16 * 8)
#ifndef PIN_DATA
#define PIN_DATA 3
#endif

CRGB leds[NUM_LEDS];

static const int W = 16;
static const int H = 8;

// drawPolygon tier: at most 4 vertices (every scene below has <= 4) and a
// 1-px column chunk — the smallest frame that still covers all scenes
// (see the budget in the file header).
static const int TIER_MAXN = 4;
static const int TIER_CHUNK = 1;

// The canvas is a view over the LED array itself — no copy anywhere.
fl::CanvasRGB canvas(fl::span<CRGB>(leds, NUM_LEDS), W, H);

typedef fl::PolyPoint<fl::s16x16> PP;

// All coordinates are s16x16 (Q16.16) — the from_raw values are exact:
// pixel p + fraction f  ->  (p * 65536 + f * 65536) rounded.
// The tables sit in flash (PROGMEM); on AVR they are staged to sStage
// before use so the span handed to drawPolygon stays a plain RAM pointer.
static const PP kBowtie[4] PROGMEM = {
    // (2,1) (14,6) (2,6) (14,1): edges AB and CD cross mid-canvas.
    { fl::s16x16::from_raw(131072L), fl::s16x16::from_raw(65536L)  },
    { fl::s16x16::from_raw(917504L), fl::s16x16::from_raw(393216L) },
    { fl::s16x16::from_raw(131072L), fl::s16x16::from_raw(393216L) },
    { fl::s16x16::from_raw(917504L), fl::s16x16::from_raw(65536L)  },
};

static const PP kSubpixel[3] PROGMEM = {
    // (2.3, 6.8) (7.6, 1.2) (12.2, 5.6) — no vertex on a pixel center.
    { fl::s16x16::from_raw(150733L), fl::s16x16::from_raw(445645L) },
    { fl::s16x16::from_raw(498074L), fl::s16x16::from_raw(78643L)  },
    { fl::s16x16::from_raw(799539L), fl::s16x16::from_raw(367002L) },
};

static const PP kClamp[3] PROGMEM = {
    // (-60,-20) (60,40) (0,100) — far off-canvas; only the clipped core
    // with two antialiased edges is visible.
    { fl::s16x16::from_raw(-3932160L), fl::s16x16::from_raw(-1310720L) },
    { fl::s16x16::from_raw(3932160L),  fl::s16x16::from_raw(2621440L)  },
    { fl::s16x16::from_raw(0L),        fl::s16x16::from_raw(6553600L)  },
};

static const PP kSliver[4] PROGMEM = {
    // (1, 4.2) (14, 4.9) (14, 5.4) (1, 4.7) — the top and bottom edges
    // each change y by < 1 px while spanning 13 px of x (dy < 65536).
    { fl::s16x16::from_raw(65536L),   fl::s16x16::from_raw(275251L) },
    { fl::s16x16::from_raw(917504L),  fl::s16x16::from_raw(321126L) },
    { fl::s16x16::from_raw(917504L),  fl::s16x16::from_raw(353894L) },
    { fl::s16x16::from_raw(65536L),   fl::s16x16::from_raw(308019L) },
};

static const PP kDegenerate[2] PROGMEM = {
    // 2 vertices: a line segment, must fill nothing.
    { fl::s16x16::from_raw(131072L), fl::s16x16::from_raw(131072L) },
    { fl::s16x16::from_raw(851968L), fl::s16x16::from_raw(327680L) },
};

static const PP kDegenerateH[3] PROGMEM = {
    // (2,4) (8,4) (14,4): horizontal-only, must fill nothing.
    { fl::s16x16::from_raw(131072L), fl::s16x16::from_raw(262144L) },
    { fl::s16x16::from_raw(524288L), fl::s16x16::from_raw(262144L) },
    { fl::s16x16::from_raw(917504L), fl::s16x16::from_raw(262144L) },
};

static const PP kDiamond[4] PROGMEM = {
    // (8, 0.5) (14.5, 4) (8, 7.5) (1.5, 4): every vertex sits exactly on
    // a row center (y = k + 0.5 px) — the row-boundary convention case.
    { fl::s16x16::from_raw(524288L), fl::s16x16::from_raw(32768L)  },
    { fl::s16x16::from_raw(950272L), fl::s16x16::from_raw(262144L) },
    { fl::s16x16::from_raw(524288L), fl::s16x16::from_raw(491520L) },
    { fl::s16x16::from_raw(98304L),  fl::s16x16::from_raw(262144L) },
};

#if defined(__AVR__)
// Stage the flash-resident vertex table into RAM. s16x16 is a single i32,
// so each point is two flash dwords.
static PP sStage[TIER_MAXN];
static const PP* stagePts(const PP* flash, int n) {
    for (int i = 0; i < n; ++i) {
        sStage[i].x = fl::s16x16::from_raw(pgm_read_dword(&flash[i].x));
        sStage[i].y = fl::s16x16::from_raw(pgm_read_dword(&flash[i].y));
    }
    return sStage;
}
#else
static const PP* stagePts(const PP* ram, int) { return ram; }
#endif

static void drawPts(const CRGB& color, const PP* pts, int n) {
    const PP* ram = stagePts(pts, n);
    canvas.drawPolygon<fl::s16x16, TIER_MAXN, TIER_CHUNK>(
        color, fl::span<const PP>(ram, n), fl::DrawMode::DRAW_MODE_OVERWRITE);
}

static void renderScene(int scene) {
    // Black = zeroed bytes (CRGB is a plain 3-byte struct).
    memset(leds, 0, sizeof leds);
    switch (scene) {
        case 0:
            drawPts(CRGB::Red, kBowtie, 4);
            break;
        case 1:
            drawPts(CRGB::Green, kSubpixel, 3);
            break;
        case 2:
            drawPts(CRGB::Blue, kClamp, 3);
            break;
        case 3:
            drawPts(CRGB::Cyan, kSliver, 4);
            break;
        case 4:
            // Dim warm base: the degenerate polygons must leave it unchanged.
            for (int i = 0; i < NUM_LEDS; ++i) leds[i] = CRGB(24, 12, 4);
            drawPts(CRGB::White, kDegenerate, 2);
            drawPts(CRGB::White, kDegenerateH, 3);
            break;
        default:
            drawPts(CRGB::Magenta, kDiamond, 4);
            break;
    }
}

static const char* sceneName(int scene) {
    switch (scene) {
        case 0: return "bowtie (self-intersect, even-odd)";
        case 1: return "subpixel (fractional vertices)";
        case 2: return "clamp (+/-60..100 px vertices)";
        case 3: return "sliver (dy < 1 px edges)";
        case 4: return "degenerate (2-pt + horizontal: no-op)";
        default: return "diamond (row-center vertices)";
    }
}

static void printHex2(int v) {
    static const char kHex[] = "0123456789ABCDEF";
    char c[3];
    c[0] = kHex[v >> 4];
    c[1] = kHex[v & 15];
    c[2] = 0;
    Serial.print(c);
}

static void printPreview(int scene) {
    Serial.print(F("scene: "));
    Serial.println(sceneName(scene));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x)
            printHex2(leds[y * W + x].r);
        Serial.println();
    }
    Serial.println(F("---"));
}

static const unsigned long kSceneMs = 3000;
static int lastScene = -1;

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<NEOPIXEL, PIN_DATA>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
    delay(500);
    Serial.println(F("PolyShow: drawPolygon edge cases, 16x8"));
}

void loop() {
    const int scene = (int)((millis() / kSceneMs) % 6);
    if (scene != lastScene) {
        lastScene = scene;
        renderScene(scene);
        printPreview(scene);
    }
    FastLED.show();
    delay(20);
}
