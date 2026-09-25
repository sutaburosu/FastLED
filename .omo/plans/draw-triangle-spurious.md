# draw-triangle-spurious - Work Plan

## TL;DR (For humans)

**What you'll get:** the near-horizontal-edge spurious-pixel bug in `fl::gfx::drawTriangle` is
captured first (a new agent-executable "screenshot" test prints ASCII render grids + writes PGM
images + counts out-of-bounds pixels against a float reference — RED on purpose), the root cause is
proved from that evidence (prime suspect: negative-fraction wrap in `renderTriangleRow`), fixed
minimally in `src/fl/gfx/primitives.h` only, and locked as a permanent `spurious == 0` regression.

**Why this approach:** the bug lives in a pure host-runnable rasterizer, so a deterministic in-test
dump is the exact, agent-readable, zero-noise screenshot (a WASM browser route is rejected —
hook-gated example + viewer rendering noise); TDD red-green is repo policy for bugs.

**What it will NOT do:** no public API change, no other primitive touched, no new `.ino`/example,
no meson edit, no browser-screenshot infra, no force-push, no `master`/`main` commits; energy
baselines re-pinned only if the fix deliberately changes pixel output.

**Effort:** 6 todos + 4 final-verifier tasks: 1 new test file (+1 include line), 1 lint-allowlist
line, analysis (no code), a header-only fix, conditional baseline re-pin, full gates.
**Risk:** Low (additive test + header-only fix, host-proven by `fl_gfx_gfx` + sanitizers).
**Decisions:** TDD red-green (user-confirmed via approval); battery = 10 concrete near-horizontal
triangles on a 48×32 canvas; work continues on branch `feat_filled_triangles` (new commits only).

## Scope

Fix the near-horizontal-edge spurious-pixel bug in `fl::gfx::drawTriangle` (filled triangle,
branch `feat_filled_triangles`), using a purpose-built, agent-executable render-dump ("screenshot")
mechanism to capture the bug first, prove the root cause, and lock the fix as a permanent regression.

### IN (the entire request)
- `tests/fl/gfx/draw_triangle_debug.hpp` — render-dump test: per-case ASCII luminance grid printed
  via `fl::printf`, P5 PGM image written via `fl::fopen` to `.cache/gfx_triangles/<case>.pgm`,
  in-test float reference (pixel-center point-in-triangle + 1.1 px AA tolerance) marking spurious
  pixels `X` / missing interior pixels `O`, with `FL_CHECK_EQ(spurious, 0)` per case. Battery of
  10 concrete near-horizontal triangles (exact coordinates in T1).
- `tests/fl/gfx/gfx.cpp` — one added `#include` line wiring the new test (target `fl_gfx_gfx`,
  auto-discovered; NO meson edit).
- `ci/lint_cpp_rs/src/lint_core/prelude_constants.rs` — one allowlist entry
  (`"draw_triangle_debug.hpp"` in `TEST_PATH_EXCLUDED_FILES`, mirroring the `draw_triangle.hpp`
  precedent at lines 471-472).
- Root-cause fix in `src/fl/gfx/primitives.h` ONLY (determined in T3 from dump evidence; prime
  candidate: negative-fraction handling in `renderTriangleRow` lines 393-421 — see T3/T4).
- Deliberate energy-baseline re-pin in `tests/fl/gfx/draw_triangle.hpp` IF AND ONLY IF the fix
  changes pixel output (T5; expected to be a no-op — the three baseline triangles use positive
  coordinates and their edges stay in positive-x).
- Full gates: `bash test fl_gfx_gfx`, `--debug`, `bash lint`, `bash compile wasm --examples Blink`,
  `/code-review`.

### OUT / Must-NOT-Have (guardrails, not scope reduction)
- NO public API change (`drawTriangle` signatures in `canvas.h`/`gfx.h` untouched).
- NO changes to any other primitive (`drawLine`/`drawDisc`/`drawRing`/`drawStrokeLine`).
- NO new `.ino`/example (hook-gated); NO WASM/browser screenshot infrastructure (rejected: the bug
  is in a pure host-runnable rasterizer — a host dump captures it exactly and is agent-readable).
- NO `meson.build` edits. NO `CanvasMapped` additions.
- NO force-push; NO commits on `master`/`main`; work stays on `feat_filled_triangles`.
- NO changes to existing test expectations in `draw_triangle.hpp`/`draw_triangle_16.hpp` other than
  the deliberate T5 energy re-pin.
- NO float/double in the rasterizer's inner loop (8.8 integer math stays integer).

## Verification strategy

**Strategy: TDD red-green** (repo policy for bugs; prior `draw-triangle` plan used tests-after for a
new feature — a bug fix locks the failing behavior first). All gates agent-executable, zero
human-in-the-loop.

- Framework: `FL_TEST_FILE` / `FL_TEST_CASE` / `FL_SUBCASE` / `FL_CHECK_EQ`; includes `test.h`,
  `fl/gfx/gfx.h`, `fl/stl/detail/file_io.h` (for `fl::fopen`, precedent `tests/fl/codec/vorbis.hpp:12`).
- Target: **`fl_gfx_gfx`** (the new `.hpp` is pulled in only by the include line in
  `tests/fl/gfx/gfx.cpp`).
- Commands (project root, bash wrappers only):
  - `bash test fl_gfx_gfx` — build+run; in T1 the new subcases are expected RED (that IS the capture);
    after T4 everything is GREEN.
  - `bash test fl_gfx_gfx --debug` — ASAN/LSAN/UBSAN re-run (mandatory repo policy on any failure;
    also the final gate).
  - `bash lint` — after the allowlist entry exists.
  - `bash compile wasm --examples Blink` — general no-regression sanity.
  - `/code-review` — over the changed files, before push.
- Invariants (concrete, agent-checkable):
  - **Spurious gate**: every battery subcase reports `spurious == 0` (the `FL_CHECK_EQ`). A pixel is
    spurious iff `lum > 0` AND its center's minimum signed edge distance `< -1.1` px (outside the
    legitimate 1-px AA fringe).
  - **Missing (informational)**: `lum == 0` with center strictly inside (`minEdgeDist > 0.25` px) is
    counted and printed as `O` — NOT gated (coverage-convention edge effects on slivers are
    acceptable); a large missing count must be explained in T3.
  - **Existing behavior preserved**: all pre-existing `draw_triangle.hpp` / `draw_triangle_16.hpp`
    subcases stay green (positive-x outputs bit-identical for a floor-vs-truncation fix).
  - **Sanitizers clean**: negative-x / clipping cases cause no OOB.
  - **Evidence artifacts**: 10 ASCII grids in the test output + 10 PGM files on disk
    (`.cache/gfx_triangles/`, gitignored via `.gitignore:78`; the directory is created by the T1
    `mkdir -p` step — the test itself never mkdirs, so the /tmp fallback covers a missing dir).
- Reference math (in-test, float allowed in tests): signed distance from pixel center to each edge
  line, normalized so inside = all three ≥ 0 (orientation fixed by sign of the triangle's area).
  `sqrtf` on edge length only (test-only cost).

## Execution strategy

One wave, sequential (analysis gates the fix). Header-only change; host test target proves it.

- Phase A (capture): T1 (RED dump test + wiring) → T2 (lint allowlist, independent).
- Phase B (analyze + fix): T3 (evidence analysis, NO code) → T4 (fix in `primitives.h`) → T5
  (baseline re-pin, conditional no-op).
- Phase C (gates): T6 (full gate set).
- Final verification wave (F1–F4) in parallel after all todos; ALL must APPROVE.

Dependency matrix:
- T1 -> none
- T2 -> none (needed before T6's lint gate)
- T3 -> T1
- T4 -> T3
- T5 -> T4
- T6 -> T2, T4, T5

## Todos

- [ ] 1. Add render-dump test `tests/fl/gfx/draw_triangle_debug.hpp` + wire into `tests/fl/gfx/gfx.cpp` (RED)
  - References: model the file on `tests/fl/gfx/draw_triangle.hpp` (includes `test.h`,
    `fl/gfx/gfx.h`; FL_TEST_FILE/CASE/SUBCASE style; `CRGB buffer[N] = {}; fl::CanvasRGB canvas(buffer, W, H);`).
    Add `#include "fl/stl/detail/file_io.h"` (defines `fl::FILE`/`fl::fopen`; precedent
    `tests/fl/codec/vorbis.hpp:12`) and `#include "fl/math/fixed_point/s16x16.h"` (case 10).
    Append `#include "tests/fl/gfx/draw_triangle_debug.hpp"` after the LAST include in
    `tests/fl/gfx/gfx.cpp` (the current final include line is `perf_primitives.hpp`; do NOT insert
    mid-file after `draw_triangle_16.hpp`). No meson edit.
    File contents (exact spec):
    - `constexpr int kDebugW = 48, kDebugH = 32;` color `CRGB(255, 0, 0)`.
    - Helpers (anonymous namespace): `int lum(const CRGB& c)` = max channel;
      `char lumChar(int v)` = `" .:-=+*#%@"[(v * 9 + 127) / 255]` (10-char scale, indices 0..9;
      the `* 9` is deliberate — `* 10` would read the NUL terminator for lum ≥ 243 and blank the
      full-brightness interior);
      `float edgeDist(px, py, ax, ay, bx, by)` = signed distance to the line A→B
      (`(dx*(py-ay) - dy*(px-ax)) / len`, `len = sqrtf(...)`);
      `float minEdgeDist(px, py, const float v[6])` = min of the three edge distances with the
      triangle's orientation normalized by the sign of `area2 = (x1-x0)*(y2-y0) - (y1-y0)*(x2-x0)`
      (if `area2 < 0`, negate all three distances) so inside = all ≥ 0.
    - `struct DumpStats { int spurious; int missing; };`
    - `DumpStats dumpCase(const char* name, CRGB* buf, const float v[6])`:
      1. `fl::printf("\n--- %s ---\n", name)`.
      2. ASCII grid: for each `y` in 0..kDebugH-1: print `"%2d |"` then for each `x`: char =
         `X` if spurious, `O` if missing, else `lumChar(lum(buf[y*kDebugW+x]))`; newline.
      3. PGM: path `fl::string(".cache/gfx_triangles/") + name + ".pgm"`;
         `fl::fopen(path.c_str(), "wb")` (fl::string has no implicit const-char* conversion);
         on failure retry once with the same name under `/tmp/gfx_triangles/` prefix; on second
         failure print a warning and continue (ASCII grid is the primary agent-readable artifact;
         PGM is for humans). Header `"P5\n48 32\n255\n"` + row-major `lum` bytes; `fl::fclose`.
      4. Per pixel center `(x+0.5, y+0.5)`: `d = minEdgeDist(...)`; spurious iff
         `lum > 0 && d < -1.1f`; missing iff `lum == 0 && d > 0.25f`.
      5. `fl::printf("spurious=%d missing=%d\n", spurious, missing)`; return stats.
    - Battery — `FL_TEST_CASE("drawTriangle near-horizontal render dump")` with 10 subcases; each:
      fresh `CRGB buffer[kDebugW*kDebugH] = {};`, `fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);`,
      one `drawTriangle` call, `DumpStats s = dumpCase(name, buffer, v); FL_CHECK_EQ(s.spurious, 0);`
      Coordinates (`x0,y0, x1,y1, x2,y2`):
      1. `top_gentle_lr` float: (1.5,1.25) (46.5,3.25) (24,30)
      2. `top_gentle_rl` float: (46.5,1.25) (1.5,3.25) (24,30)
      3. `top_same_row` float: (1.5,1.25) (46.5,1.75) (24,30)
      4. `top_exact_horiz` int: (1,2) (46,2) (24,30)
      5. `bottom_gentle` float: (24,1.5) (1.5,29.5) (46.5,30.5)
      6. `sliver_flat` float: (0.5,2.5) (47.5,4.5) (23.5,5.75)
      7. `negx_flat` float: (-5,2) (30,3) (12,28)
      8. `tall_sliver_flat` int: (23,1) (25,2) (24,31)
      9. `top_gentle_lr_int` int: (1,1) (46,3) (24,30)
      10. `top_gentle_lr_q16` fl::s16x16: (1.5,1.25) (46.5,3.25) (24,30)
  - Acceptance: from project root, `mkdir -p .cache/gfx_triangles` (the test never mkdirs — `fl`
    has no directory API and host fopen does not create parents; this step makes the PGM path
    writable), then `bash test fl_gfx_gfx` builds and runs; all 10 subcases print their ASCII grid +
    `spurious=N missing=M` line; PGM files exist under `.cache/gfx_triangles/` (or the /tmp
    fallback, warning printed); at least one subcase FAILS its `FL_CHECK_EQ(spurious, 0)` (RED
    captured — record which subcases + sample failing rows as evidence).
  - QA happy: build green, 10 grids printed, PGMs on disk, ≥1 RED subcase with spurious rows
    visible as `X` runs.
  - QA failure: NO subcase is RED → the battery missed the user's scenario; extend the battery with
    up to 5 more near-horizontal variants (e.g. edge starting at the canvas left boundary, smaller
    Δy, near-horizontal edge as the triangle's only short edge) and re-run; the RED capture is the
    exit condition, not the file count.
  - Commit: `test(gfx): add near-horizontal drawTriangle render dump + spurious-pixel reference`

- [ ] 2. Allowlist the new test file for the C++ lint path-structure checker
  - References: `ci/lint_cpp_rs/src/lint_core/prelude_constants.rs` — `TEST_PATH_EXCLUDED_FILES`
    array; insert `"draw_triangle_debug.hpp",` immediately after `"draw_triangle_16.hpp",`
    (line 472). Mirror of the `d48ba8b6ab` precedent.
  - Acceptance: `bash lint` exits clean (TestPathStructureChecker does not flag
    `tests/fl/gfx/draw_triangle_debug.hpp`).
  - QA happy: `bash lint` clean.
  - QA failure: lint flags the file with a different rule → read the rule's message, add the
    minimal matching allowlist entry (same file), re-run.
  - Commit: `ci(lint): allowlist draw_triangle_debug.hpp for TestPathStructureChecker`

- [ ] 3. Analyze the dumps: capture the spurious-span pattern and root cause (NO code changes)
  - References: run `bash test fl_gfx_gfx`; read the 10 ASCII grids + `spurious=` lines; open the
    PGMs for the failing cases if useful. Implementation under diagnosis:
    `src/fl/gfx/primitives.h` — `renderTriangleRow` (393-421) and `drawTriangleCore` (967-1043).
    Ranked candidate root causes (verify against the dumps, do NOT assume):
    1. **Negative-fraction wrap** (prime): `renderTriangleRow` lines 399-402 compute
       `xLi = xL8 >> 8` / `xRi = xR8 >> 8` (truncation toward zero) and `Lfrac/Rfrac = x8 - (xi<<8)`;
       for negative edge-x, the fraction is NEGATIVE, and
       `nscale8(static_cast<fl::u8>(negative))` (lines 413, 419) wraps to a large bogus weight →
       spurious bright pixels exactly on rows where an edge sits off-canvas-left; single-column
       branch `cw = Rfrac - Lfrac` (line 405) is likewise out of 0..255.
    2. Truncation-vs-floor `xLi > xRi` skip / span endpoints for mixed-sign spans (line 403-411).
    3. Anything else the dumps reveal (row range / half-open split / accumulator) — the
       accumulator math was hand-verified correct for positive-x cases during planning, so weight
       the candidates accordingly.
  - Acceptance (deliverable, no code): (a) list of failing subcases with the failing rows quoted
    from the ASCII grids; (b) one-paragraph root-cause statement naming the exact
    `primitives.h` lines and the arithmetic that produces the bogus pixels;     (c) a hand-computed
    repro for the FIRST failing row of any failing case: take that row from the dump, recompute its
    `xL8`/`xR8` from the accumulator init/step (primitives.h 1009-1042), then show the exact
    weight computation that produced the bogus pixel (which `Lfrac`/`Rfrac`/`cw` value, which
    `nscale8` input, which u8 it became, which pixel it landed on) matching the observed `X`
    position; (d) the exact fix to apply.
  - QA happy: every observed `X` in every failing case is explained by the root-cause statement
    (no unexplained pixels).
  - QA failure: an observed `X` row cannot be explained → re-read the failing case's edge x-values
    row by row (compute `xL8/xR8` at the row center by hand from the accumulator init/step) until
    the mechanism is pinned; do not proceed to T4 with an unexplained pixel.
  - Commit: none (analysis only; the diagnosis rides in the T4 commit message).

- [ ] 4. Fix the root cause in `src/fl/gfx/primitives.h` (GREEN)
  - References: `src/fl/gfx/primitives.h` `renderTriangleRow` (393-421) — apply the T3 fix. If T3
    confirmed candidate 1, the reference fix (keep the exact style, integer-only, no float in the
    loop): replace the truncating split at lines 399-402 with a floor split for BOTH edges, e.g.
     local helper `static inline int floorDiv8(fl::i32 v) { return (v >= 0) ? (v >> 8) : (v == INT32_MIN ? -8388608 : -((-v + 255) >> 8)); }`
    then `int xLi = floorDiv8(xL8); fl::i32 Lfrac = xL8 - (fl::i32(xLi) << 8);` (and same for
    `xRi`/`Rfrac`) — for positive values this is bit-identical to today (`>> 8` == floor), for
    negative values it yields `Lfrac/Rfrac ∈ [0,255]` and the `xLi == xRi` weight
    `Rfrac - Lfrac ∈ [0,255]` falls out correctly. If T3 found a different root cause, apply THAT
    fix (T3's deliverable (d) is the spec) with the same minimal-footprint rules.
    Must-hold: no public API change; no other primitive touched; no float in the inner loop;
    writes only via `addPixelToBuffer<PixelT, Overwrite>`.
  - Acceptance: `bash test fl_gfx_gfx` green on everything EXCEPT the T5 energy baselines —
    every one of the 10 dump subcases `spurious == 0` AND every pre-existing `draw_triangle.hpp` /
    `draw_triangle_16.hpp` subcase passes, with the three T5 energy `FL_CHECK_EQ`s allowed to
    mismatch (T5 re-pins them if — and only if — the fix changed that pixel output); `bash test
    fl_gfx_gfx --debug` sanitizer-clean (no OOB on the negative-x / clipping cases).
  - QA happy: all subcases green in one run (energy baselines green or pending T5); grids show no `X`.
  - QA failure: any dump subcase still spurious → do not patch the test; re-run T3 on the residual
    `X` rows (loop T3→T4); any pre-existing subcase regressed OTHER than the three T5 energy
    baselines → the fix changed positive-x behavior — revert and re-derive from T3 (a correct
    floor split cannot change positive-x output).
  - Commit: `fix(gfx): <one-line root cause from T3> in drawTriangle near-horizontal rows` (commit
    message body: T3 diagnosis + failing-row repro).

- [ ] 5. Re-pin drawTriangle energy baselines — conditional no-op
  - References: `tests/fl/gfx/draw_triangle.hpp` — the `drawTriangle pixel-exact energy`
    `FL_TEST_CASE` prints three energies and asserts them with `FL_CHECK_EQ` (T6 of the prior
    plan; baseline triangles `T_a (2,2)(13,2)(2,13)`, `T_b (3.5,4.5)(12.5,5.5)(6.5,11.5)`,
    `T_c (8.25,16.75)(24.5,7.5)(18.25,23.25)` — all positive coordinates).
  - Acceptance: run `bash test fl_gfx_gfx`. If all three `FL_CHECK_EQ`s pass → todo is a NO-OP
    (record the green output as evidence; no commit). If any mismatches → confirm the delta is the
    intended fix (dump the affected baseline triangle before/after with the T1 helpers), update
    exactly those `FL_CHECK_EQ` values, re-run green. Prior-plan T6 rule: never update a baseline
    silently — the update is deliberate and the pixel change is explained.
  - QA happy: all three energies match (no-op) OR updated values pass.
  - QA failure: an energy mismatches but the pixel delta cannot be explained by the fix → stop and
    re-run T3 (the fix has unintended side effects).
  - Commit (only if values changed): `test(gfx): re-pin drawTriangle energy baselines after near-horizontal fix`

- [ ] 6. Final gates: full test + sanitizers + lint + WASM sanity + code review
  - References: from project root (bash wrappers only): `bash test fl_gfx_gfx`;
    `bash test fl_gfx_gfx --debug`; `bash lint`; `bash compile wasm --examples Blink`; then the
    `/code-review` skill over the changed files (`tests/fl/gfx/draw_triangle_debug.hpp`,
    `tests/fl/gfx/gfx.cpp`, `ci/lint_cpp_rs/src/lint_core/prelude_constants.rs`,
    `src/fl/gfx/primitives.h`, and `tests/fl/gfx/draw_triangle.hpp` if T5 changed it).
  - Acceptance: ALL green — every subcase passes, sanitizers clean, lint clean, WASM compiles,
    /code-review reports no blocking findings.
  - QA happy: every command exits clean.
  - QA failure: any red → fix root cause (re-run `--debug` if a test failed), then re-run ALL gates.
  - Commit: none (verification only — fold any surfaced fix into its owning todo's commit).

## Final verification wave

- [ ] F1. Plan compliance audit — re-read this plan + `git diff master..HEAD`; confirm every IN item
  exists and every Must-NOT-Have is absent (no API change, no other primitive touched, no new .ino,
  no meson edit, no CanvasMapped, no browser-screenshot infra). Evidence: `git diff --stat` +
  targeted grep.
- [ ] F2. Code quality review — run the repo `/code-review` skill over the changed files; confirm the
  fix matches the T3 root-cause diagnosis (not a test-weakening), conventions (span/naming/
  FL_NO_EXCEPT/IWYU), no float in the rasterizer inner loop, bounds only via `addPixelToBuffer`,
  reference math confined to the test file.
- [ ] F3. Real manual QA (agent-executed, NO eyeballing required) — `bash test fl_gfx_gfx` green with
  0 spurious across all 10 subcases, `--debug` sanitizers clean, `bash lint` clean,
  `bash compile wasm --examples Blink` compiles; 10 PGM files present under `.cache/gfx_triangles/`
  (or /tmp fallback) and 10 ASCII grids in the captured output; the T3 failing-row repro is quoted
  in the fix commit message.
- [ ] F4. Scope fidelity — confirm the ONLY changed paths are `src/fl/gfx/primitives.h`,
  `tests/fl/gfx/draw_triangle_debug.hpp`, `tests/fl/gfx/gfx.cpp`,
  `ci/lint_cpp_rs/src/lint_core/prelude_constants.rs`, and (only if T5 fired)
  `tests/fl/gfx/draw_triangle.hpp`; branch is `feat_filled_triangles`; nothing on `master`/`main`.

## Commit strategy

Branch is `feat_filled_triangles` (already pushed to origin — regular `git push` only, NEVER
force-push, never touch `master`/`main`). Conventional commits, one per todo:
- `test(gfx): add near-horizontal drawTriangle render dump + spurious-pixel reference` (T1)
- `ci(lint): allowlist draw_triangle_debug.hpp for TestPathStructureChecker` (T2)
- `fix(gfx): <root cause> in drawTriangle near-horizontal rows` (T4; body carries the T3 diagnosis)
- `test(gfx): re-pin drawTriangle energy baselines after near-horizontal fix` (T5, only if it fired)
Then `git push`; if a PR is already open on `feat_filled_triangles` it updates automatically,
otherwise `gh pr create` (the prior boulder's flow was push + PR).

## Success criteria

- `bash test fl_gfx_gfx` green: the 10 near-horizontal battery subcases report `spurious == 0`
  (the reported bug is gone), all pre-existing triangle subcases unchanged, `--debug` clean,
  `bash lint` clean, WASM sanity compiles.
- A root-cause diagnosis with a hand-computed row repro exists in the fix commit message; the fix
  is confined to `src/fl/gfx/primitives.h` and changes no positive-x output (floor-vs-truncation).
- Screenshot mechanism in place and demonstrated: ASCII grids in test output + PGM files on disk for
  10 near-horizontal cases, usable for future rasterizer regressions.
- Energy baselines deliberate (unchanged, or re-pinned with explanation); scope-fidelity clean;
  branch pushed with the new commits.
