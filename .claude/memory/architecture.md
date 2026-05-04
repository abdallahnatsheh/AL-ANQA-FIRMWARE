---
name: T-DECK-CLI Code Architecture
description: File structure, global object pattern, command system, and coding conventions
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
**Source dir:** `t-deck-cli/` (set in platformio.ini `src_dir`)

**Global objects** defined in `main.ino`, accessed via `extern` in all other `.cpp` files:
```
LGFX tft
DisplayManager displayManager(tft)
CommandManager commandManager
InputHandling inputHandler
ESPInfoPrinter espInfoPrinter(displayManager)
WiFiFunctions wifiFunctions(displayManager)
BluetoothFunctions bluetoothFunctions
```

**Command registration:** all commands registered in `CommandManager::setupCommands()` in `command_manager.cpp`. Use `registerCommand(name, shortName, lambda, description, hasArgs)`. Max 64 commands. Buffer is 128 bytes (was 64, needs increasing).

**Input loop pattern:** every blocking scan function must poll `inputHandler.getKeyboardInput()` for `'q'` to allow the user to exit. Navigation uses `a`=prev, `l`=next, `q`=quit, `u`=update.

**Display rules:** all output via `displayManager`, never directly to `tft`. Key methods:
- `println()` / `printText()` — output
- `clearScreen()` — clears output area only
- `tdeck_begin()` — full reset to home screen
- `printCommandScreen()` — prints `CMD> ` prompt

**File convention:** each feature module = one `.cpp` + one `.h` pair.

**String types:** use Arduino `String` for display/WiFi work. Avoid mixing `std::string` and `String` in the same function — causes implicit conversions and heap churn.

**No Serial debug in production:** strip all `Serial.println` debug lines before merging to main. Never print passwords or credentials to Serial.

**How to apply:** Follow this pattern for every new module. New commands always go in setupCommands(). Always poll inputHandler in loops.
