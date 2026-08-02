---
name: usblan plan (USB-LAN adblocker / DNS-MITM dongle)
description: SHELVED (too complex vs payoff) — usblan/adblock: T-Deck as USB Ethernet gadget bridging PC↔WiFi with an SD-sourced DNS sinkhole / DNS-MITM. Research captured 2026-07-12.
type: project
---

**Command idea: `usblan` / `adblock` (`ubl`)** (Network, [EXP]). **SHELVED 2026-07-12** — user judged the
integration cost too high for the payoff (see FEASIBILITY). This doc preserves ALL the research so it can
be picked up later without re-deriving it. Nothing was HW-tested. next_steps.md #24 is the origin.

## Concept
T-Deck plugs into a PC over USB and presents as a **USB Ethernet adapter** (NCM; RNDIS for old Windows).
The PC routes ALL its internet **through** the T-Deck, which is on WiFi as a STA (`cw`). A **DNS
interceptor** sits in the middle:
- **Benign (adblock)** — blocklisted domains → `0.0.0.0`/NXDOMAIN, else forwarded upstream.
- **Offensive (DNS-MITM)** — log every query to SD + redirect chosen domains to the T-Deck's own IP →
  eviltwin/karma captive portal.
```
PC ──USB(NCM)──► T-Deck ──WiFi(STA)──► router ──► internet
                   │ usb  esp_netif 192.168.7.1  (DHCP + DNS server)
                   │ wlan esp_netif (STA, real IP)
                   │ NAPT bridge usb↔wlan
                   └ DNS proxy :53 → sinkhole / log / redirect
```
**The user specifically wants the USB form** (not the WiFi ARP/DNS-spoof form — they already know that
one). NAT is intrinsic to the USB form: the PC's only uplink is the T-Deck, so ALL its traffic (not just
DNS) must be forwarded to WiFi = NAT. No lighter USB trick avoids it (L2-bridging a PC MAC onto WiFi-STA
isn't supported by normal APs — that's why every ESP router uses L3 NAT).

## RESEARCH: s60sc/ESP32_AdBlocker (the "reuse" candidate)
- **License AGPL-3.0** — matches Al-Anqa.
- **Standalone Arduino APPLICATION, not a library.** Complete DNS-sinkhole *server*: point a router/device
  DNS at the ESP's IP → returns `0.0.0.0` for blocklisted domains, forwards the rest upstream.
- **DNS ONLY.** No NAT, no DHCP, **no USB, no captive-portal, no router.** Contributes ZERO to the hard
  USB-NCM↔WiFi NAT bridge.
- Blocklist in **PSRAM (mandatory)**, **<50µs lookups on S3** / <100µs ESP32; size by PSRAM (8MB → Plus).
- **Requires Arduino-ESP32 core ≥ 3.1.1.** Al-Anqa is `espressif32@7.0.1` = Arduino **2.0.x / IDF 4.4**
  (confirmed by CLAUDE.md's CSI note "IDF-4.4 fields … renames in IDF 5.2+" + the pinned gWpaSm offset).
  → **core mismatch, can't drop the sketch in.** Port the CONCEPT (DNS parse → PSRAM blocklist → forward),
  ~a couple hundred lines, easier fresh than back-porting. AdBlocker = design reference + perf target only.

## DESIGN DECISIONS (settled with the user)
1. **NO web UI.** AdBlocker's web server exists only because it's headless. T-Deck has a screen +
   keyboard + trackball → all config/stats native on-screen / CLI (`displayManager`, no Serial). Dropping
   HTTP is a simplification (less DRAM, one fewer thing on the WiFi/GDMA path).
2. **Blocklist on SD, indexed into PSRAM at boot** (NOT per-query SD reads):
   - `/apps/usblan/blocklist.txt`, plain domains, user-editable on a PC (fits the `/apps/<tool>/` model).
   - Per-query SD lookup NOT viable — SD is SPI, shared w/ display+LoRa, ~ms/read, collides with GDMA rule;
     DNS needs <50µs and a page = dozens of queries. SD is **storage only**.
   - On boot, stream the file → compact in-RAM index in PSRAM:
     - **Sorted 64-bit domain hashes:** 200k × 8B ≈ **1.6 MB PSRAM**, binary search, ZERO false positives.
       (Recommended default.)
     - **Bloom filter:** few hundred KB, O(1), tiny tunable false-positive rate. Smaller, less exact.
   - Net: leaner than AdBlocker (no HTTP, SD-sourced, ~1.6MB PSRAM — fine on the Plus's 8MB; mind Al-Anqa
     already uses PSRAM for LGFX sprites + wardrive dedup).

## FEASIBILITY VERDICT — silicon YES, current Arduino stack NO
**Hardware: possible.** ESP32-S3 = native USB-OTG FS + WiFi + 8MB PSRAM (Plus). NCM/RNDIS works on S3;
proven by Espressif `usb_ncm`/`usb_dongle` examples + ThingPulse dongle. **A near-ready base exists on
IDF 4.4** (see below) — the hard 90% (WiFi↔USB NAT) is already done there.
**On Al-Anqa's PlatformIO + Arduino-2.0.x / IDF-4.4 build: NOT without leaving the precompiled-lib stack.**
Two blockers, both = compile-time config frozen into the PRECOMPILED Arduino libs:
1. **lwIP NAPT is NOT linkable.** `ip_napt_enable()` is declared in headers but its impl is MISSING from
   arduino-esp32's precompiled lwIP → hard `undefined reference to 'ip_napt_enable'` (arduino-esp32 issues
   **#6421, #8193**). NAPT needs `CONFIG_LWIP_IP_FORWARD` + `CONFIG_LWIP_IPV4_NAPT` +
   `CONFIG_LWIP_L2_TO_L3_COPY` — sdkconfig baked into the precompiled libs, un-flippable from a sketch.
2. **TinyUSB NCM net class isn't exposed** by the Arduino USB wrapper (only CDC/MSC/HID). The class must be
   enabled at tinyusb-config level, also compile-time in the precompiled stack.

**Why a hand-added custom .c does NOT solve it:** NAPT/NCM are NOT leaf functions — they're core-path
features whose CALL SITES live in already-compiled TUs. `ip_napt_enable()` lives in `ip_napt.c` but it's
*invoked* from `ip4.c`'s `ip4_forward()`/`ip4_input()` under `#if IP_NAPT`, and that `ip4.c` is inside the
precompiled `liblwip.a` built with IP_NAPT **off** → the calls were compiled out. You can define the
function but the pre-built forwarder never calls it (and IP_FORWARD may be off → packets dropped at
input). Same for NCM: TinyUSB's class-driver dispatch table is pre-baked from `CFG_TUD_NCM`. You can add
leaf code, but you can't make frozen core code call it. Mixing a NAPT-on TU against NAPT-off precompiled
libs also risks struct/ABI mismatch → memory corruption.

## VERSION FACTS (decisive)
- **NAPT:** natively available in **IDF 4.4** lwIP (no upgrade needed).
- **USB net class (NCM):** official `espressif/tinyusb` net class needs **IDF ≥ 5.0**. On **IDF 4.4** use
  the community **`leeebo/tinyusb_src`** backport instead.
- **Arduino-as-IDF-component** works but has known gotchas (e.g. arduino-esp32 #9334 "LWIP requires ipv6").

## HOW TO SOLVE THE BLOCKERS — routes (least→most disruptive)
Both blockers share ONE root cause: frozen compile-time config in the precompiled Arduino libs. Fix = get
control of the build config.
- **Route 1 — `esp32-arduino-lib-builder` (recommended for a native Al-Anqa command).** Rebuild the SAME
  versions (Arduino 2.0.x / IDF 4.4.7) with the 4 flags ON (+ `leeebo/tinyusb_src` for the net class on
  4.4), point PlatformIO at the custom libs. Same framework version → **gWpaSm offset (netspy) + CSI 4.4
  fields stay valid**. Changes are **additive/dormant** (NAT runs only when usblan calls `ip_napt_enable`;
  every other tool links a behaviorally-identical lib). **One-time** build; after that Al-Anqa builds exactly
  as today + `usblan` works. Low risk IF the WHOLE consistent set is rebuilt (avoids ABI mismatch);
  regression-test once (sw/cw/deauth/ws/pm/wardrive, sbl/bi, ux/MSC, ns, csi).
- **Route 2 — Arduino-as-IDF-component.** Restructure Al-Anqa as an IDF project using arduino-esp32 as a
  source component → sdkconfig is yours. Same win, whole-project build migration, more disruptive.
- **Route 3 — IDF 5.x + Arduino core 3.x.** Cleanest tinyusb support BUT **breaks pinned stuff** (gWpaSm
  offset shifts, CSI fields renamed post-5.2, Arduino 2→3 API churn). Highest risk. Avoid.
- **Dual-partition boot-switch** (Al-Anqa + separate usblan IDF app in 2 partitions, a command switches boot
  partition + reboots): zero risk to Al-Anqa, but **user REJECTED it** ("dual boot not good for a simple
  tool").

## THE TWO SHAPES (there is no "simple AND inside Al-Anqa" — NAT can't link in Arduino)
- **Shape 1 — one Al-Anqa firmware, `usblan` a native command.** Needs Route 1 (one-time lib rebuild). Only
  way to have USB-NAT in the running firmware.
- **Shape 2 — a dedicated dongle firmware you flash when needed.** Simplest to BUILD (near-ready IDF-4.4
  base exists; add DNS). But it's a separate firmware you swap in (not "dual boot" partitions — a purpose
  firmware, like flashing Marauder vs Bruce).

## NEAR-READY BASES (the hard 90% already done, on our IDF version)
- **ThingPulse/esp32-pendrive-s3-wifi-dongle** — ESP-IDF **v4.4**, ECM/RNDIS, explicitly "support host to
  surf the internet wirelessly via USB" (the WiFi↔USB NAT bridge). Partially complete; no explicit DHCP/DNS
  described. Closest starting point.
- **espressif/esp-iot-solution `usb/device/usb_dongle`** (the `add_usb_solutions` branch) — the example
  ThingPulse is based on; STA + USB-ECM/RNDIS, `sta`/`smartconfig` commands.
- Caveat from research: bare NCM examples ("tusb_ncm") give USB-to-a-webserver-on-ESP but NOT internet;
  the dongle examples add the NAT that makes the host actually get online.

## VERIFIED APIs (so a future build isn't fiction)
`esp_tinyusb` net (from esp-usb `tinyusb_net.h`):
```c
typedef struct { uint8_t mac_addr[6]; tusb_net_rx_cb_t on_recv_callback;
                 tusb_net_free_tx_cb_t free_tx_buffer; tusb_net_init_cb_t on_init_callback;
                 void *user_context; } tinyusb_net_config_t;
esp_err_t tinyusb_net_init(const tinyusb_net_config_t *cfg);
esp_err_t tinyusb_net_send_sync(...timeout...);  esp_err_t tinyusb_net_send_async(...);
esp_err_t tinyusb_net_deinit(void);
```
Bridge wiring: create a custom `esp_netif` (static 192.168.7.1, ETH netstack, DHCP-server flag); its
transmit cb → `tinyusb_net_send_sync`; USB `on_recv_callback` → `esp_netif_receive(usb_netif,...)`;
`esp_netif_dhcps_start`; `esp_netif_napt_enable(usb_netif)` (IDF ≥5) or raw `ip_napt_enable()` (4.4).
sdkconfig: `CONFIG_LWIP_IP_FORWARD/IPV4_NAPT/L2_TO_L3_COPY=y`, `CONFIG_TINYUSB_NET_MODE_NCM=y`
(or `_RNDIS` for Windows). A ~200-line spike (WiFi STA → USB-NCM esp_netif+DHCP → NAPT, no DNS yet) was
scaffolded then removed on shelving; recreate from the ThingPulse base if resumed.

## RECOMMENDED PATH IF RESUMED
1. **Spike** off the ThingPulse/iot-solution base (IDF, standalone) — confirm a PC gets online through the
   T-Deck. Make-or-break; base suggests it works.
2. Add **DNS sinkhole/redirect + SD blocklist → PSRAM index** on top → working dongle (Shape 2).
3. Only if you want it as a native Al-Anqa command → **Route 1 one-time lib rebuild** (Shape 1).

## WiFi ALTERNATIVE (user already knows; NOT what they want, kept for context)
Every other ESP LAN-MITM tool does it over WiFi, no USB/NAPT/lib-rebuild: **ARP-poison a victim on the
joined WiFi + forge its DNS** (sinkhole or redirect-to-portal). Pure Arduino, reuses cw + raw injection +
eviltwin portal; already backlog #2/#3 (`arpspoof`/`lanmitm`). Refs: GhostESP (ARP poison/DNS intercept/
MITM over WiFi+W5500), Bruce (ARP poison/evil portal), CapibaraZero/ARP_Poisoner (ESP32 STA), martin-ger/
esp32_nat_router (STA+SoftAP NAT, IDF). Limit (matches isoscan honesty): single-radio ARP MITM — Windows
ignores unsolicited ARP, full forwarding imperfect; DNS-spoof specifically is the reliable subset.

## SD sketch
`/apps/usblan/blocklist.txt` (source), `config.conf`, `redirect.txt` (offensive), `dns_NNN.csv` (query log).

## SOURCES (verified 2026-07-12)
- s60sc/ESP32_AdBlocker — https://github.com/s60sc/ESP32_AdBlocker (AGPL, DNS-only, PSRAM, core ≥3.1.1)
- platform-espressif32 7.0.1 = Arduino 2.0.17 — https://github.com/platformio/platform-espressif32/releases
- arduino-esp32 NAPT not-linkable — issues #6421, #8193 · as-IDF-component caveat #9334
- esp_tinyusb net needs IDF ≥5.0; v4.4 via `leeebo/tinyusb_src` — https://components.espressif.com/components/espressif/tinyusb
- IDF 4.4 lwIP NAPT — https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-guides/lwip.html
- ThingPulse esp32-pendrive-s3-wifi-dongle (IDF 4.4, ECM/RNDIS, internet-over-USB) — https://github.com/ThingPulse/esp32-pendrive-s3-wifi-dongle
- esp-iot-solution usb_dongle · Espressif `usb_ncm`/`tusb_ncm` examples
- WiFi alt refs: GhostESP (ghostesp.net) · Bruce (wiki.bruce.computer) · CapibaraZero/ARP_Poisoner ·
  martin-ger/esp32_nat_router · esp_tinyusb API (espressif/esp-usb tinyusb_net.h)
- Related memory: [[project_usb_gadget_plan]] (MSC+HID branch + SPI CS fixes), eviltwin/karma portals.
