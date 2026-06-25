---
name: UI Rules
description: Header style + cursor corruption fix — apply to every new command screen
type: feedback
---

## Header Style (every command screen)
```cpp
dm.setTextColor(0x7BEF); dm.printText("[");
dm.setTextColor(TFT_CYAN); dm.printText("VERB");   // SCAN, SPAM, ATCK, INFO
dm.setTextColor(0x7BEF); dm.printText("::");
dm.setTextColor(TFT_YELLOW); dm.printText("NOUN"); // WIFI, BLE, FP, SYS
dm.setTextColor(0x7BEF); dm.printText("]  ");
dm.println(pgBuf);   // "01/02" — snprintf(pgBuf, 8, "%02d/%02d", page+1, total)
dm.printSeparator();
```
Error: `[ERR::CMD]` with `TFT_RED` for `ERR`.

## Input keys — what the T-Deck keyboard actually has (Abdallah, emphatic)
- **NO Esc key. NO arrow keys.** The I2C keyboard sends printable chars, `\b`, `\r`/`\n` only.
  Never design a prompt/loop whose cancel/back is Esc (27) — it can't be pressed.
- **Cancel/back = trackball CLICK** (the editor's idiom). In a *text-entry* prompt, click cancels
  (can't use a letter — it'd be typed). In a *selectable list*, `q` is fine for cancel (not typing).
- **Navigation = trackball** UP/DOWN/L/R + CLICK (via `getTrackballEvent()`), NOT arrow keys.
- Autocomplete key is `'` (Sym+K = 0x27), defined `KEY_AUTOCOMPLETE`.

## Live/auto-updating lists are hard to use — prefer MANUAL refresh
Abdallah on macwatch add-mode: a list that re-sorts/redraws in real time is impossible to read and
pick from. **Scan once → freeze a stable list → let the user navigate/pick at their pace → an
explicit key (e.g. `[u]`) rescans.** Don't auto-refresh a screen the user is trying to select on.

## Cursor Corruption Fix
`getCursorY()` returns the raw TFT cursor Y. Status bar redraws (battery, GPS, clock) write at y<30 and leave the TFT cursor there. Any `setCursor(x, getCursorY())` called AFTER a `getKeyboardInput()` loop will print into the battery icon.

**Rule: save Y ONCE before any poll loop, use fixed value inside:**
```cpp
dm.println("Enter value:");
int32_t inputY = dm.getCursorY();   // ← before any getKeyboardInput()

while (true) {
    char c = inputHandler.getKeyboardInput();  // may corrupt tft cursor
    if (!c) continue;
    // redraw at fixed Y — never call getCursorY() here
    dm.fillRect(10, inputY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
    dm.setCursor(10, inputY);
    dm.printText(c);
}
```

**For page-table prompts:** save `promptY = dm.getCursorY()` immediately after `renderPage()` — before the inner key-wait loop. Safety clamp: `if (promptY < outputY) promptY = SCREEN_HEIGHT - LINE_HEIGHT * 2;`
