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
