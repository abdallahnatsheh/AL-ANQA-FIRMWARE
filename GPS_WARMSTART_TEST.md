# GPS Warm Start — Manual Test Steps

## First Boot (Cold Start — no NVS data yet)

1. Flash firmware and power on T-Deck Plus
2. Run `gpson`
3. Module name should show `L76K +BDS+SBAS` or `M10Q @...` — NO "warm" suffix
4. Go outside, wait for fix (~3-4 min expected)
5. Once `FIX OK` appears, note how long it took
6. Press `q` to exit (GPS keeps running in background)

## Second Boot (Warm Start — NVS has data)

7. Run `gpsoff` then power cycle the device
8. Run `gpson`
9. Module name should now show `L76K +BDS+SBAS warm` or `M10Q @... warm`
10. Go outside — fix should arrive in **30-90 seconds** instead of 3-4 min

> If "warm" suffix appears but fix still takes 4 min, the module's VBAT backup
> pin may not be supplying power — the L76K BBR was cleared on power-off.

## Verify NVS Saved Correctly

11. After step 5 (first fix), power off and wait 10 minutes
12. Power on and run `gpson` — if "warm" suffix appears, the NVS write succeeded

## TrackMe Sanity Check

13. Run `tm` — should start normally
14. If GpsManager background task is already running, TrackMe reuses it
15. If not, TrackMe inits its own GPS (uses BDS constellation now)
16. Confirm: no crash, no freeze, location updates normally

## Edge Cases

17. Remove SD card, run `gpson` — must still work (warm start uses NVS, not SD)
18. Run `gpson` twice without power cycle — second call should print
    "GPS BACKGROUND — already running" and reuse the running task
