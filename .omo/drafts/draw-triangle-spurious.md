# draw-triangle-spurious - Draft

status: approved
intent: clear
review_required: false
classification: standard
pending-action: write .omo/plans/draw-triangle-spurious.md
branch: feat_filled_triangles (pushed to origin; NEW COMMITS ONLY, never force-push)

## Components (topology lock)
- C1 render-dump — agent-executable "screenshot" mechanism for canvas triangles: ASCII brightness grid + P5 PGM file + in-test float reference diff. Status: planned.
- C2 analysis — capture the spurious spans on near-horizontal-edge rows; identify root cause from dump evidence (no armchair root-cause). Status: pending C1.
- C3 fix — minimal root-cause fix in src/fl/gfx/primitives.h. Status: pending C2.
- C4 regression+gates — permanent spurious==0 regression test, deliberate energy-baseline re-pin, full gates (test/--debug/lint/wasm/code-review), commits on feat_filled_triangles. Status: pending C3.

## Findings (evidence, with paths)
- Prior work: boulder `draw-triangle` completed 2026-09-18 (`.omo/boulder.json`); plan `.omo/plans/draw-triangle.md` (full rasterizer spec + energy baselines).
- Branch state (git log): `feat_filled_triangles` = master `1aaee3a62c` + 8 commits: 7 drawTriangle commits (`c016c6d8f0` row renderer → `d48ba8b6ab` CI allowlist) + 1 general CI fix (`852358c57c`). Pushed. Working tree clean except untracked `.omo/`.
- Feature surface: `src/fl/gfx/primitives.h` (+150: `TriCtx` 279-284, `renderTriangleRow` 393-421, `drawTriangleCore` 967-1043, free `drawTriangle` 709-718), `src/fl/gfx/canvas.h` (+11), `src/fl/gfx/gfx.h` (+8), `tests/fl/gfx/draw_triangle.hpp` (+220), `tests/fl/gfx/draw_triangle_16.hpp` (+52), `tests/fl/gfx/gfx.cpp` (+2 includes), `ci/lint_cpp_rs/src/lint_core/prelude_constants.rs` (`TEST_PATH_EXCLUDED_FILES` lines 471-472).
- Test target: `fl_gfx_gfx` (auto-discovered; new `.hpp` pulled in only by a `#include` line in `tests/fl/gfx/gfx.cpp`; no meson edit).
- Test file I/O available: `fl::FILE*` / `fl::fopen` from `src/fl/stl/detail/file_io.h` (POSIX = std FILE) — PGM dump feasible inside the test binary.
- No existing screenshot mechanism in repo (grep: none); no stb_image_write vendored (`src/third_party/stb/` has vorbis only).
- WASM browser-screenshot route evaluated and REJECTED: requires new/hook-gated example (`ci/hooks/protect_example_ino.py`), adds viewer rendering layers (noise), and the bug lives in a pure host-runnable rasterizer — a host dump captures it exactly and is agent-readable.
- Static analysis (read-only): accumulator math in `drawTriangleCore` hand-verified CORRECT for positive-x near-horizontal cases (init + per-row 8.8 step, truncation < 1/256 px/row). PRIME SUSPECT: negative-fraction handling in `renderTriangleRow` (lines 399-402: `xLi/xRi = x8 >> 8` truncates toward zero; `Lfrac/Rfrac = x8 - (xi<<8)` NEGATIVE when edge x < 0; `nscale8(static_cast<fl::u8>(neg))` wraps to a large bogus weight; single-column `cw = Rfrac - Lfrac` out of 0..255 range). Secondary suspects: `xLi==xRi` branch with mixed-sign fracs; `xLi > xRi` skip vs truncated negatives. Root cause NOT confirmed — C2 dumps decide.
- Symptom (user): near-horizontal edge involved → many spurious pixels on affected row(s), spans outside triangle bounds.

## Decisions (adopted defaults, announced — not forks; user delegated mechanism via "find a way")
- D1 Screenshot mechanism = HOST TEST DUMP: new `tests/fl/gfx/draw_triangle_debug.hpp` wired into `tests/fl/gfx/gfx.cpp`; per case prints an ASCII luminance grid via `fl::printf` and writes P5 PGM via `fl::fopen("wb")` to `.cache/gfx_triangles/<case>.pgm`; in-test float reference (pixel-center point-in-triangle + 1.1px AA tolerance band) marks spurious pixels 'X' / missing 'O' and counts them.
- D2 Test strategy = TDD red-green: dump test asserts spurious==0 → FAILS first (captures the bug, RED) → fix → GREEN → pinned as permanent regression.
- D3 Battery: 48×32 canvas, CRGB(255,0,0), zero-init per case, ~10 concrete near-horizontal triangles (gentle top edge L→R and R→L, top edge within one row, exactly-horizontal top, near-horizontal bottom, thin sliver, negative-x flat top, tall thin flat-top sliver, float + int + s16x16 coord variants).
- D4 Energy baselines in `tests/fl/gfx/draw_triangle.hpp` re-pinned DELIBERATELY only if the fix changes pixel output (prior plan T6 rule: confirm intentional, then update).
- D5 Commits (conventional, mirror prior feature): test+allowlist commit(s) → `ci(lint)` allowlist commit (separate dir, mirror `d48ba8b6ab`) → fix commit → baseline commit (only if values move).
- D6 Scope guardrails: no public API change, no new `.ino`, no meson edits, no new examples, no changes to other primitives; fix confined to `primitives.h` (+ test files).

## Approval gate
- Brief presented; user replied "approve" (2026-09-23).
- Approval authorizes writing `.omo/plans/draw-triangle-spurious.md` ONLY. No implementation by planner, ever.

## Metis gap analysis (mandatory) — completed
- Session: ses_f31c46d39ffe5Z6A4n67WjMCsF (read-only; git status unchanged).
- Findings: 1 blocking (B1 PGM dir never created → added `mkdir -p .cache/gfx_triangles` to T1
  acceptance + Verification artifacts), 5 should-fix (S1 lumChar `*10`→`*9` NUL off-by-one;
  S2 gfx.cpp include placement → after `perf_primitives.hpp`, verified line 9; S3 T4 acceptance
  contradicted T5 re-pin → relaxed to "all green except T5 energy baselines"; S4 T3 hand-computed
  example was not derived from case 7 → replaced with generic recompute-from-dump procedure;
  S5 TL;DR placeholder → filled), 1 nice-to-have (N1 `fl::fopen(path.c_str(), "wb")`).
- All folded into `.omo/plans/draw-triangle-spurious.md`. Structural self-check (task-row grammar,
  header order) performed after edits.
- review_required stays false (no high-accuracy modifier; CLEAR intent); dual high-accuracy review
  OFFERED at handoff, not run.

## Extra facts locked during plan writing
- `fl::fopen` include: `#include "fl/stl/detail/file_io.h"` (precedent: `tests/fl/codec/vorbis.hpp:12`).
- `.cache` is gitignored (`.gitignore:78`) → PGM dumps never committed.
- Plan scaffold hand-written (no bash available in planner session); template headers verbatim per skill.

## High-accuracy dual review — completed (2026-09-25)

### Momus (adversarial) — APPROVED
- Session: ses_f27468aa6ffezX7ku0cbT7KgEa (1h 28m)
- Verdict: APPROVED — plan is sound end-to-end.
- All reference chains verified correct (floorDiv8 math ✓, battery design ✓, scope ✓, T3→T4 handoff ✓).
- INT32_MIN UB noted but deemed dead code (accumulator bounds ~±262K for canvas ≤ 48×32).
- No blocking findings.

### Oracle (independent) — CHANGES_REQUESTED → resolved
- Session: ses_f274624c1ffeTf6ZqNZpgESP51 (1h 5m)
- Verdict: CHANGES_REQUESTED — floorDiv8 formula has UB at INT32_MIN due to negation overflow.
- **Fix applied:** Plan updated (line 221) to include `INT32_MIN` guard:
  `(v >= 0) ? (v >> 8) : (v == INT32_MIN ? -8388608 : -((-v + 255) >> 8))`
- Optional improvement noted (add all-negative-x test case) — accepted as low priority, not blocking.
- All other findings: confirmed correct (scope precise, test battery sufficient, T3→T4 robust, executable).

### Plan integrity
- SHA256 of updated plan: recompute before execution.
- Plan file: `.omo/plans/draw-triangle-spurious.md` (updated with INT32_MIN guard).
- All Momus + Oracle findings resolved. Plan is APPROVED for execution.
