---
name: project-gps-warmstart
description: GPS warm start — NVS lat/lon cache, L76K BeiDou/SBAS, M10Q MGA-INI inject
---

Implemented in `gps_manager.cpp/.h`.

- **L76K:** BeiDou `$PCAS04,7*1E`, SBAS `$PMTK313,1*2E`+`$PMTK301,2*2E`, flash save `$PCAS00*01`; warm start `$PCAS10,0*1C` if prior NVS fix
- **M10Q:** UBX-MGA-INI-POS_LLH (0x13/0x40) position inject → UBX-CFG-RST hot start; applied after each `recoverUblox()`
- **NVS namespace `"gps"`:** keys `"fix"` (bool), `"lat"` (int32 ×1e7), `"lon"` (int32 ×1e7) — written on first fix, read on boot
- `trackme.cpp` also sends `$PCAS04,7*1E` for `_ownGps` init
- NVS used (not SD) because GDMA rule makes SD writes unsafe while WiFi is active
