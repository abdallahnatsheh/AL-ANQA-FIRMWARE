---
name: feedback_user_compiles_manually
description: Do NOT run pio/PlatformIO builds — the user compiles and flashes manually
metadata:
  type: feedback
---

The user builds and flashes the firmware **themselves**. Do NOT run `pio run` /
`platformio run` / any compile or upload command — they have rejected compile
attempts multiple times (2026-07-05).

**Why:** they have their own build/flash setup and each flash is a deliberate HW
test step for them; an agent-run compile is noise. They also prefer a thorough
STATIC review ("verify code works before compile") over burning a build.

**How to apply:** after writing/editing firmware code, do a careful static pass
(verify APIs/symbols exist, buffer sizes, includes, dispatch) and hand off — say
it's ready to flash. Don't offer to compile. Trust that "it compiles manually"
on their side; rely on CI (build.yml builds both envs on push) as the automated
gate. See [[feedback_rules]].
