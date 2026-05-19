---
name: Cyberpunk header style — all screens
description: Every command screen uses [VERB::NOUN]  01/02 header with specific colors — must be consistent
type: feedback
---

All scan/info/help/error screens use this exact header pattern:

```cpp
dm.setTextColor(0x7BEF);     dm.printText("[");
dm.setTextColor(TFT_CYAN);   dm.printText("VERB");      // e.g. SCAN, INFO, HELP
dm.setTextColor(0x7BEF);     dm.printText("::");
dm.setTextColor(TFT_YELLOW); dm.printText("NOUN");      // e.g. WIFI, BLE, SYS
dm.setTextColor(0x7BEF);     dm.printText("]  ");
dm.setTextColor(0x7BEF);     dm.println(pgBuf);         // "01/02" zero-padded
```

Error notice uses `[ERR::CMD]` with `TFT_RED` for `ERR`.

**Why:** Established across scanwifi, scanblue, netdiscover, info pages, help, hiddenssid — must stay consistent or screens look mismatched.

**How to apply:** Every new command screen gets this header on the first line. NOUN is uppercase. Page indicator format: `snprintf(pgBuf, 8, "%02d/%02d", page+1, total)`. `printSeparator()` goes on the next line.
