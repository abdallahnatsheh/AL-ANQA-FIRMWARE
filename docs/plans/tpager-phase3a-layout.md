# Plan: T-Pager Phase 3a — responsive layout helpers + bottom-anchored reflow

## Goal
Introduce `core/board/layout.h` and migrate every screen off bare 320×240-era pixel literals onto helpers that read `metrics.h`. Two invariants:

1. **T-Deck / T-Deck-Plus render byte-for-byte identically** after the change. Every helper compiles to the exact literal that used to be there (validated arithmetically per site below).
2. **T-Pager visible bugs fixed** — the footers at Y≥222 that are currently drawn off-screen on the 480×**222** display become visible, anchored to the actual bottom of the screen.

**Scope of this plan: Tier 1 (bottom-anchored Y-literals only) + Tier 2 (editor/ssh column width).** Widescreen redesigns (tables gaining columns, two-pane, sprite centering, home-grid rework) stay for Phase 3b — they need per-screen thought, not a mechanical helper. See "Explicitly out of scope" below.

**This is a plan only. No code is written here.** Execution stays on `feature/pentest-enhancements`; the user compiles/flashes; CI is the compile-gate.

---

## Verified findings from the sweep

### Actually invisible on T-Pager today (drawn past pixel 221)

| Site | Y | Content | Files |
|---|---|---|---|
| csidetect radar footer | 230 | `a/l sens c=cal t=auto n=nbvi s=log q` | [csidetect.cpp:544,553,555](al-anqa-firmware/wifi/sensing/csidetect.cpp:544) |
| isoscan attack menu / running-tool footer | 230 | `[k] keyid`, `[q] back to menu`, `[q] save + back` | [isoscan.cpp:136,176,242,359,462,660,768,1005,1173](al-anqa-firmware/wifi/attacks/isoscan/isoscan.cpp) (9 sites) |
| bad_usb result footer | 226 | script status | [bad_usb.cpp:484](al-anqa-firmware/usb/bad_usb/bad_usb.cpp:484) |
| macwatch background bar | 222 | popup dark-green bar | [macwatch.cpp:262](al-anqa-firmware/bluetooth/tools/macwatch/macwatch.cpp:262) |
| espchat background bar | 222 | popup bar | [espchat_bg.cpp:108](al-anqa-firmware/radio/espnow/espchat/espchat_bg.cpp:108) |
| wguard popup bar | 222/223 | popup bar + text | [wguard.cpp:1141,1142](al-anqa-firmware/wifi/defense/wguard/wguard.cpp:1141) |

### 0-margin on T-Pager (draws at 214..221 — fits, but future `println()` misuse triggers the [cast bomb](../CLAUDE.md) `scrollIfNeeded` clear at y>208)

| Site | Y | Files |
|---|---|---|
| dpwo footers | 210/212/214 | [dpwo.cpp:858,894,895,903,904](al-anqa-firmware/wifi/attacks/dpwo/dpwo.cpp) |
| wpa3down footers ×10 | 210/212/214 | [wpa3down.cpp:238,239,330,331,460,462,486,487,570,572,645,647](al-anqa-firmware/wifi/attacks/wpa3down/wpa3down.cpp) |
| arpspoof footer | 214 | [arpspoof.cpp:313](al-anqa-firmware/wifi/attacks/arpspoof/arpspoof.cpp:313) |
| responder footer | 214 | [responder.cpp:299](al-anqa-firmware/wifi/attacks/responder/responder.cpp:299) |
| wps footer | 214 | [wps.cpp:323](al-anqa-firmware/wifi/attacks/wps/wps.cpp:323) |
| wifi_functions (`sw`) footers | 200/202/210/214 | [wifi_functions.cpp:282,285,304,305](al-anqa-firmware/wifi/core/wifi_functions.cpp:282) |
| bad_usb header/hints | 202/212/214 | [bad_usb.cpp:362,363,481,482](al-anqa-firmware/usb/bad_usb/bad_usb.cpp:362) |

### Editor / SSH terminal — real UX regression, not clipping

- [text_editor.cpp](al-anqa-firmware/core/editor/text_editor.cpp) `ED_COLS=52` — wraps typing at 52 chars on a 480 px screen with room for ~80. **~35% of typing width lost.**
- [ssh_client.cpp](al-anqa-firmware/wifi/tools/sshcon/ssh_client.cpp) `COLS=52` — SSH terminal shows ~65% of what should be an 80-col line, breaking most server prompts.

### Already responsive (no change needed)

- [netspy.cpp:534,593,627](al-anqa-firmware/wifi/intel/netspy.cpp:534) uses `SCREEN_HEIGHT - 14` and `SCREEN_HEIGHT - 10` — carries over correctly.
- 114 `SCREEN_WIDTH - N` fillRect sites — all correct on both boards.
- All `outputY + N * LINE_HEIGHT` row math is metric-derived and safe.

---

## Helpers to add — `al-anqa-firmware/core/board/layout.h`

Header-only, `constexpr` / `static inline` (zero code cost, compiles to the same literal at each call site). Reads `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `LINE_HEIGHT`, `outputY`, `promptHeight` from the active variant's `metrics.h` via `board.h`.

```cpp
#pragma once
#include "board.h"          // pulls in the active variant's metrics.h

// Bottom-anchored Y for a footer element `k` px above the bottom edge.
//   T-Deck  (240): footerY(30) → 210, footerY(26) → 214, footerY(10) → 230
//   T-Pager (222): footerY(30) → 192, footerY(26) → 196, footerY(10) → 212
constexpr int layoutFooterY(int k) { return SCREEN_HEIGHT - k; }

// Right-anchored X for an element `w` px wide against the right edge.
constexpr int layoutRightX(int w)  { return SCREEN_WIDTH  - w; }

// Text-grid columns for the 6px Font0 grid (editor / SSH terminal).
// Preserves T-Deck's ED_COLS=52 exactly; T-Pager gets ~78.
constexpr int layoutCharCols()     { return (SCREEN_WIDTH - 8) / 6; }

// (Grid row / colX / rowsPerPage helpers deliberately DEFERRED to Phase 3b —
// RPP tuning is per-screen, not mechanical. Adding them here would tempt a
// premature reflow that Phase 3b will have to redo.)
```

**Design principle** (matches the T-Pager port plan §"HAL design"): helpers are board-agnostic; they read metrics, they never branch on board identity. Adding a third board = fill in `metrics.h` only.

### Why only these three helpers now
The port plan suggested `rowY(n)`, `colX(i,ncols)`, `rowsPerPage()`, `charCols()` too. Restraint here:
- `rowY(n)` — the `outputY + n * LINE_HEIGHT` pattern in-place is already correct and readable. Wrapping it doesn't fix any bug and forces every consumer to `#include <layout.h>`.
- `colX(i,ncols)` — most existing column layouts are content-tuned (`DP_COL_MARK=4`, `CX_MAC=52`), not evenly-spaced. A blind `colX()` reflow would make tables *look worse*. Phase 3b redesigns tables properly.
- `rowsPerPage()` — needs a per-screen `(headerRows, footerRows)` pair that's not obvious mechanically. Phase 3b.

Ship the three helpers that pay for themselves *this pass*; leave the rest to when they earn their `#include`.

---

## Reflow plan — file by file

Every replacement below is verified arithmetically: the T-Deck literal after the change must equal the literal before. The T-Pager literal shifts to `222 - k`.

### Tier 1 — bottom-anchored reflow (bug-fix + regression-prevention)

| File | Line | Before | After | T-Deck (240) | T-Pager (222) |
|---|---|---|---|---|---|
| [csidetect.cpp:544](al-anqa-firmware/wifi/sensing/csidetect.cpp:544) | | `setCursor(PANEL_X, 208)` | `setCursor(PANEL_X, layoutFooterY(32))` | 208 ✓ | 190 (was off) |
| csidetect.cpp:553 | | `fillRect(0, 229, W, 11, …)` | `fillRect(0, layoutFooterY(11), W, 11, …)` | 229 ✓ | 211 (was off) |
| csidetect.cpp:555 | | `setCursor(6, 230)` | `setCursor(6, layoutFooterY(10))` | 230 ✓ | 212 (was off) |
| isoscan.cpp × 9 sites (136,176,242,359,462,660,768,1005,1173) | | `setCursor(6, 230)` | `setCursor(6, layoutFooterY(10))` | 230 ✓ | 212 (was off) |
| macwatch.cpp:262 | | `fillRect(0, 222, W, 16, 0x0240)` | `fillRect(0, layoutFooterY(18), W, 16, 0x0240)` | 222 ✓ | 204 (was off) |
| espchat_bg.cpp:108 | | `fillRect(0, 222, W, 16, bgCol)` | `fillRect(0, layoutFooterY(18), W, 16, bgCol)` | 222 ✓ | 204 (was off) |
| wguard.cpp:1141 | | `fillRect(0, 222, 320, 16, …)` | `fillRect(0, layoutFooterY(18), SCREEN_WIDTH, 16, …)` | 222 ✓ | 204 (was off); also `320`→`SCREEN_WIDTH` for width parity |
| wguard.cpp:1142 | | `setCursor(4, 223)` | `setCursor(4, layoutFooterY(17))` | 223 ✓ | 205 (was off) |
| bad_usb.cpp:484 | | `setCursor(6, 226)` | `setCursor(6, layoutFooterY(14))` | 226 ✓ | 208 (was off) |
| bad_usb.cpp:362 | | `setCursor(0, 202)` | `setCursor(0, layoutFooterY(38))` | 202 ✓ | 184 (0-margin → 4 px gap) |
| bad_usb.cpp:363 | | `setCursor(6, 214)` | `setCursor(6, layoutFooterY(26))` | 214 ✓ | 196 |
| bad_usb.cpp:481 | | `setCursor(0, 202)` | `setCursor(0, layoutFooterY(38))` | 202 ✓ | 184 |
| bad_usb.cpp:482 | | `setCursor(6, 212)` | `setCursor(6, layoutFooterY(28))` | 212 ✓ | 194 |
| dpwo.cpp:858 | | `setCursor(0, 210)` | `setCursor(0, layoutFooterY(30))` | 210 ✓ | 192 |
| dpwo.cpp:894 | | `fillRect(0, 212, W, LH, …)` | `fillRect(0, layoutFooterY(28), W, LH, …)` | 212 ✓ | 194 |
| dpwo.cpp:895 | | `setCursor(6, 214)` | `setCursor(6, layoutFooterY(26))` | 214 ✓ | 196 |
| dpwo.cpp:903,904 | | (same as 894/895) | (same helpers) | | |
| wpa3down.cpp × 12 sites | | `210/212/214` | `layoutFooterY(30/28/26)` | ✓ | 192/194/196 |
| arpspoof.cpp:313 | | `setCursor(10, 214)` | `setCursor(10, layoutFooterY(26))` | 214 ✓ | 196 |
| responder.cpp:299 | | `setCursor(10, 214)` | `setCursor(10, layoutFooterY(26))` | 214 ✓ | 196 |
| wps.cpp:323 | | `setCursor(6, 214)` | `setCursor(6, layoutFooterY(26))` | 214 ✓ | 196 |
| wifi_functions.cpp:280 | | `setCursor(0, 192)` | `setCursor(0, layoutFooterY(48))` | 192 ✓ | 174 |
| wifi_functions.cpp:282 | | `setCursor(6, 200)` | `setCursor(6, layoutFooterY(40))` | 200 ✓ | 182 |
| wifi_functions.cpp:285 | | `setCursor(6, 214)` | `setCursor(6, layoutFooterY(26))` | 214 ✓ | 196 |
| wifi_functions.cpp:304 | | `fillRect(0, 202, W, SCREEN_HEIGHT-202, TFT_BLACK)` | `fillRect(0, layoutFooterY(38), W, 38, TFT_BLACK)` | 202/38 ✓ | 184/38 |
| wifi_functions.cpp:305 | | `setCursor(6, 210)` | `setCursor(6, layoutFooterY(30))` | 210 ✓ | 192 |

**Count: ~41 line edits across 13 files.** Every one is a literal-preserving replacement on T-Deck.

**Nice property — relative row spacing is preserved for free.** A stacked footer block like bad_usb's 3-row y=202/212/226 becomes footerY(38/28/14); on either board the row-to-row deltas are +10 / +14, because `(SH−a) − (SH−b) = b−a` and SH cancels. No per-board spacing math needed for grouped footers.

### Tier 2 — text-grid columns (real UX gain on T-Pager, byte-identical T-Deck)

| File | Line | Before | After | T-Deck | T-Pager |
|---|---|---|---|---|---|
| text_editor.cpp | `#define ED_COLS 52` | `#define ED_COLS 52` | `constexpr int ED_COLS = layoutCharCols();` | 52 ✓ | 78 |
| ssh_client.cpp | `#define COLS 52` | (same) | `constexpr int COLS = layoutCharCols();` | 52 ✓ | 78 |

**Two edits.** Both files already have their internal row/scroll/hit-test math derived from `COLS`, so widening it is a one-line change per file. **User-visible wins:** editor gains 26 chars per line; SSH terminal renders a normal 80-col session.

**`constexpr` in an array bound — verified safe.** `ssh_client.cpp` declares `static char s_buf[SB][COLS]` and `static uint8_t s_col[SB][COLS]` at file scope. `constexpr int COLS = layoutCharCols();` is a compile-time constant expression (chain: `#define SCREEN_WIDTH 320` → literal → `constexpr` helper → `constexpr int` = usable as an array bound, same category as `#define`). No dynamic allocation, no VLA.

**DRAM footprint impact — noted, fits.** `s_buf` + `s_col` at file scope live in DRAM/BSS. At SB=120:
- T-Deck (COLS=52): 2 × 120 × 52 = **12.5 KB**
- T-Pager (COLS=78): 2 × 120 × 78 = **18.7 KB** (+6.2 KB)

Both trivial vs the ~320 KB internal DRAM budget. text_editor has no such array (uses `std::vector<String>` heap), so no impact there.

### Explicitly out of scope (Phase 3b)

- **Sprite widths / positions**: pwn HUD `BOX_W=216`/`RX=226`, csidetect radar `SPR_W=198`, buddy `BUDDY_X_CENTER=77`. Off-center on T-Pager but not broken. Redesign, not reflow.
- **Table columns**: `DP_COL_STAT=96`, `bmon CX_*`, `wm`/`netspy` column layouts. Under-uses width. Redesign, not reflow.
- **RPP constants**: `BMON_ROWS_PER_PAGE=7`, `KM_RPP=7`, etc. Per-screen tuning.
- **Home launcher grid** 4×2. Redesign to 5×2 or 6×2 = design call.
- **Status-bar tune** — already done in the earlier port work.
- **NES/game HUD** — no bare Y-literals worth touching.

Locking Phase 3a to helpers + literal reflow is the safety guarantee. Anything that could *change layout on the T-Deck* gets deferred.

---

## The safety net — how we prove T-Deck is unchanged

### 1. Arithmetic invariant (per-site)
Every table row above lists both the old T-Deck literal and the new expression's value at T-Deck metrics. If they don't match, that row is a bug — that's the review gate for the diff.

### 2. Compile-gate grep (post-edit)
```
grep -rEn "(setCursor|fillRect)\([^,]+,\s*(2[0-3][0-9])" al-anqa-firmware --include="*.cpp"
```
should return an **empty list** except for known-responsive sites (netspy's `SCREEN_HEIGHT - N` and any newly-audited exception). Any remaining bare Y≥200 is either a missed site or a deliberate exception documented in the commit.

### 3. CI compile-gate
Both `env:T-Deck` and `env:T-Deck-Plus` (and `env:T-Pager`) must build green. No new warnings.

### 4. User flash sequence (the definitive check)
- **First**: flash T-Deck. Run the affected commands: `dw`, `w3d`, `as`, `rsp`, `wps`, `sw`, `csi`, `is` (menu + several attacks), `mw` popup, `bs`/`bd`/pwn (uses ec/mw bars), `bg`, `wg` popup, `ux`. Every footer must land at the same pixel it did before. Any visible diff = bug.
- **Then**: flash T-Pager. The seven "invisible today" sites must render visibly at the bottom. Nothing else should have moved.

### 5. Rollback plan
If any T-Deck screen looks different post-flash: `git revert` the single commit that introduced the helper adoption for that file, keep the rest, ship a fix. Because each file's edits are localized, per-file revert is clean.

---

## Execution order (when the plan is approved)

1. **Add `core/board/layout.h`** (the three helpers, no other changes).
2. **Tier 1 reflow, one file per commit** — 13 small commits, in this order (least-risky-first, so a bad commit is easy to bisect):
   1. `wps.cpp` — 1 site
   2. `arpspoof.cpp` — 1 site
   3. `responder.cpp` — 1 site
   4. `macwatch.cpp` — 1 site
   5. `espchat_bg.cpp` — 1 site
   6. `wguard.cpp` — 2 sites (also width literal `320`→`SCREEN_WIDTH`)
   7. `bad_usb.cpp` — 5 sites
   8. `csidetect.cpp` — 3 sites
   9. `wifi_functions.cpp` — 4 sites
   10. `dpwo.cpp` — 5 sites
   11. `wpa3down.cpp` — 12 sites (biggest, so it lands with the pattern well-verified)
   12. `isoscan.cpp` — 9 sites
3. **Tier 2 (COLS)** — 2 commits (editor, then ssh); each is independently flashable + revertible.
4. **Grep verification**: run the sweep from §"Safety net" — must be empty of new bare Y≥200 in touched files.
5. **User flash + eyeball** on T-Deck, then T-Pager.
6. **Doc updates** in the same PR:
   - Append a "Phase 3a: layout.h — done" bullet to [t-pager-port.md](t-pager-port.md).
   - Save a memory that `layout.h` is the seam for future footer additions ("never write a bare y≥200; use `layoutFooterY(k)`").

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| One helper call has arithmetic drift on T-Deck | Low | High (visual regression) | Per-site arithmetic proof in the table above; grep-audit post-edit; user flashes T-Deck first |
| `constexpr` COLS breaks a `static` sizing that expects a literal | Low | Med | Editor uses `std::vector<String>` (heap), no `[COLS]` array; SSH `s_buf[SB][COLS]` = fine (`constexpr` is a compile-time literal, same category as `#define`) |
| An off-screen footer moving on-screen collides with another element on T-Pager | Low | Low | New footer Y at 192–212 lands in what is currently blank canvas on T-Pager; visual verification during flash |
| A file uses a Y-literal I didn't grep for (e.g., in a lambda, inline string) | Low | Med | Compile-gate grep uses a broad regex; anything missed surfaces on the T-Deck flash pass |
| Someone writes a new footer at `y=230` later | High | Low | Save a memory + add a CI grep as a follow-up (out of scope for this PR) |

## Honesty / hard truths
- **This is not glamorous work.** ~40 line edits + 3 helpers. But it fixes 7 categories of invisible-on-T-Pager UI and pays regression-insurance on the exact bug class that took `cast` down last month.
- **It does not make anything on the T-Pager *look* good** — Phase 3b does that. It makes it *correct* + *safe to touch*.
- **The T-Deck byte-identical invariant is the whole safety story** — the user has said the T-Decks are "perfect" and any regression there is worse than the T-Pager gain. Every arithmetic check above enforces that.

## Success criteria
- All three helpers exist in `layout.h`; no other new file.
- 13 files edited, ~40 lines changed, arithmetic-preserving on T-Deck.
- Two `COLS` edits widen editor/SSH on T-Pager only.
- CI green on all three envs.
- User flash: T-Deck unchanged; T-Pager 7 categories of footer newly visible.
- Follow-up memory saved: `layout.h` is the seam for bottom-anchored UI.

## What's next after this lands
Phase 3b — the widescreen redesign work this plan deliberately did not touch: table column layouts (`wm`/`bmon`/`netspy`/`dpwo`/`isoscan`), sprite recentering (`pwn`/`csidetect`/`buddy`), home-launcher grid, list+detail two-pane for the tools that most want it. That plan lives in a separate document when we get to it.
