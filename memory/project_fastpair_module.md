---
name: fastpair-module
description: "Fast Pair attack suite (fp command) — scan/spam/GATT hijack, NimBLE TaskManager pattern, WhisperPair GATT probe, SD paths"
metadata:
  node_type: memory
  type: project
  originSessionId: f2a28e73-2aae-4b57-a11c-57573886b78c
---

`fastpair/fp` — `fast_pair.cpp/.h`, `fast_pair_keys.h`, registered in `command_manager.cpp`, man page in `man_pages.cpp`.

**Subcommands:** `scan` (BLE passive scan for FP advertisements), `spam` (Google Fast Pair flood with per-cycle MAC randomization), `h <idx>` (GATT WhisperPair probe on scanned device), `h all` (probe all scanned devices). From scan results: `[s]` → spam, `[h+#]` → hijack, `[A]` → all.

**Scan pattern — NimBLE TaskManager:**
`start()` with any duration blocks via `ulTaskNotifyTake` on the calling thread — duration=0 would block indefinitely (watchdog crash). Fix: `fpScanTaskFn` runs `scan->start(5, false)` in a FreeRTOS sub-task spawned via `TaskManager::start()`. Main thread polls `TaskManager::isRunning()` with spinner + q-abort — same pattern as `sbl`.

```cpp
static void fpScanTaskFn(void* param) {
    NimBLEScan* scan = static_cast<NimBLEScan*>(param);
    scan->start(5, false);
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}
// main: TaskManager::start(fpScanTaskFn, "fpscan", pScan, TASK_STACK_DEFAULT, 0);
```

**Spam pattern:**
- `NimBLEDevice::init("")` before loop (cold-boot safety — first cycle does deinit, must have prior init)
- Per-cycle: `deinit(true)` → 20ms → `init("")` → advertise — MAC randomizes each cycle (Android deduplicates Fast Pair by MAC+modelId pair)
- Ad format: `setFlags(0x06)` + UUID list `0xFE2C` + Service Data `0xFE2C` + 3-byte modelId — no TX power (BruceDevices exact format)
- `setMinInterval(32); setMaxInterval(48)` — minimum valid 32 units (20ms)
- After loop: `deinit(true)` → 50ms → `init("T-REX")` → `fpSdRemount()` (SD.begin(39))

**GATT attack (WhisperPair / CVE-2025-36911):**
1. Connect via NimBLE client to service `0000FE2C-...` char `FE2C1234-...`
2. Try reading anti-spoofing public key directly (64 or 65 bytes with `0x04` prefix)
3. Fall back to SD cache (`/fastpair_keys.csv`)
4. Send seeker ephemeral ECDH public key (67-byte KBP hello: `[0x00][0x00][0x04][X32][Y32]`)
5. Device responds with its own key outside pairing mode → VULNERABLE (CVE-2025-36911)
6. Key cached to SD → can be used for MIC verification in future pairing sessions

**Android 16 note:** Fast Pair popup on Android 16 requires anti-spoofing GATT key-exchange — advertisement-only spam (`fp spam`) will NOT trigger popup. Use `fp h <idx>` (GATT attack) for Android 16+. Microsoft Swift Pair (`bs ms`) has no anti-spoofing and still works.

**SD paths:**
- `/fastpair_keys.csv` — cached anti-spoofing keys: `modelId(hex),name,key(128 hex chars)`
- `/fastpair_paired.csv` — log of successfully probed devices
- `/logs/fastpair.csv` — scan log: `mac,modelId,name,rssi,status`

**Known device table:** `fast_pair_keys.h` — `FP_KNOWN_DEVICES[]` / `FP_KNOWN_COUNT`, `fpLookupName(modelId)` — maps modelId to human-readable device name for scan display.

**fpSdRemount:** `{ SD.begin(39); }` — called after any GATT attack (NimBLE deinit disturbs SPI on ESP32-S3). NOT called in `scan()` abort path — scan never calls deinit so SD is never disturbed.

See [[esp32s3-wifi-sd-gdma-constraint]] for the BLE+SD GDMA interaction rule.
