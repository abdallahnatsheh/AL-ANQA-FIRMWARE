# T-Rex — WPA3 Transition Mode Downgrade Attack Plan
# CVEs: CVE-2019-9494 through CVE-2019-9499 (Dragonblood)
# References:
#   TrustedSec July 2024 writeup
#   RedLegg June 2025 eaphammer demonstration
#   VSMtripathi/WPA3-Transition-mode-Downgrade-attack (GitHub)

---

## Overview

WPA3 transition mode (SAE+PSK mixed mode) allows both WPA3 and WPA2
clients to connect to the same network for backward compatibility.
This backward compatibility is a fundamental weakness — it can be
exploited to force a WPA3-capable client to downgrade to WPA2,
capturing a crackable handshake that WPA3-only mode would never expose.

**The attack in one sentence:**
Deauth victim from real AP → spin up WPA2-only rogue AP with same SSID
→ victim reconnects via WPA2 → capture EAPOL handshake → crack offline.

**Real world impact:**
Most enterprise and home networks in 2025-2026 are in transition mode.
Aruba, Ubiquiti, MikroTik, Cisco Meraki — all confirmed vulnerable.

---

## New Command: `wpa3down`

```
CMD> wpa3down
```
Alias: `w3d`

Integrates into existing T-Rex WiFi attack workflow:
- Extends `scanwifi` to detect and flag transition mode APs
- Extends existing evil twin / deauth infrastructure
- Adds PMF detection from beacon RSN IE parsing
- Saves captured handshake as HCCAPX to SD for Hashcat

---

## Part 1 — PMF Detection (extends `scanwifi`)

### What to detect from beacon frames

Every AP advertises its PMF status in the RSN Information Element (IE)
inside beacon and probe response frames. Two bits in RSN Capabilities:

```
Bit 6: MFPR — Management Frame Protection Required
       1 = PMF mandatory, deauth BLOCKED
       0 = check MFPC

Bit 7: MFPC — Management Frame Protection Capable
       1 = PMF supported but optional
       0 = PMF not supported, deauth WORKS
```

Combined interpretation:
```
MFPR=0, MFPC=0  → PMF disabled    → deauth works      → label [OPEN]
MFPR=0, MFPC=1  → PMF optional    → deauth may work   → label [PMF?]
MFPR=1, MFPC=1  → PMF required    → deauth BLOCKED    → label [PMF!]
```

### AKM Suite detection for WPA3 transition mode

The AKM (Auth Key Management) Suite Selector in RSN IE reveals:
```
AKM 0x000FAC02 = WPA2-PSK (CCMP)
AKM 0x000FAC04 = WPA2-PSK (CCMP) with SHA256
AKM 0x000FAC08 = SAE (WPA3-Personal)

Transition mode = BOTH 0x000FAC02 AND 0x000FAC08 present in AKM list
Pure WPA3       = ONLY 0x000FAC08 in AKM list
Pure WPA2       = ONLY 0x000FAC02 in AKM list
```

### Updated scanwifi display

Add new columns to the existing scan table:

```
# SSID              CH  RSSI  AUTH      PMF   STATUS
1 CoffeeShop_WiFi   6  -52   WPA2      OFF   [OPEN]     ← standard deauth works
2 OfficeNetwork     1  -61   WPA3+WPA2 OPT   [TD][PMF?] ← transition mode, test PMF
3 HomeNetwork       11 -71   WPA3      REQ   [PMF!]     ← pure WPA3, deauth blocked
4 GuestWiFi        6  -68   WPA2      REQ   [PMF!]     ← WPA2 with PMF required
5 Hotel_Guest      6  -45   WPA2      OFF   [OPEN]     ← easy target
```

Flags:
- `[TD]` = Transition mode detected (SAE+PSK) → downgrade viable
- `[PMF!]` = PMF Required → standard deauth blocked
- `[PMF?]` = PMF Optional → deauth likely works, test first
- `[OPEN]` = No PMF → deauth works
- `[WPA3]` = Pure WPA3 only → downgrade not possible

### How to parse RSN IE on ESP32

Use promiscuous mode to capture raw beacon frames.
RSN IE has element ID 0x30. Parse manually from beacon payload:

```
Beacon frame structure:
  - Fixed params (12 bytes)
  - Tagged params starting at byte 12
  - Each tagged param: [ID 1 byte][Length 1 byte][Data N bytes]
  - RSN IE: ID = 0x30
  - Inside RSN IE:
    - Version (2 bytes)
    - Group Cipher Suite (4 bytes)
    - Pairwise Cipher Suite Count (2 bytes)
    - Pairwise Cipher Suites (N × 4 bytes)
    - AKM Suite Count (2 bytes)
    - AKM Suites (N × 4 bytes)  ← check for SAE + PSK here
    - RSN Capabilities (2 bytes) ← MFPC = bit 7, MFPR = bit 6
```

Parse this in promiscuous mode callback when frame subtype == 8 (Beacon).

---

## Part 2 — PMF Probe Test

Before launching the attack, T-Rex tests whether deauth actually works
against the target. This is faster than assuming based on beacon flags
because buggy PMF implementations exist.

### PMF probe procedure

1. Send 3 deauth frames to a client associated with target AP
2. Monitor for that client's re-association request
3. If client re-associates within 3 seconds → PMF not enforced → proceed
4. If no re-association → PMF working → fall back to alternatives

```
[*] Testing PMF enforcement on AA:BB:CC:DD:EE:FF...
[+] Client disconnected and re-associated → PMF NOT enforced
    → Standard deauth will work for downgrade attack

[!] Client did not disconnect → PMF enforced
    → Trying pre-association flooding...
```

---

## Part 3 — Deauth Strategy Decision Tree

```
Target AP detected as [TD] transition mode
         ↓
Parse beacon RSN IE
         ↓
    MFPR = 1?
   /          \
  YES          NO
   ↓            ↓
PMF required  MFPC = 1?
Deauth        /        \
blocked      YES        NO
   ↓          ↓          ↓
   ↓       PMF optional  PMF disabled
   ↓       Test with     Deauth works
   ↓       PMF probe     directly ✅
   ↓          ↓
   ↓       Probe result?
   ↓       /          \
   ↓    Works ✅    Blocked
   ↓                   ↓
   ↓──────────→ Pre-association flood
                       ↓
              Send deauths during
              authentication window
              before PMF activates
```

### Pre-association flooding (fallback for PMF-required)

PMF protects management frames after association.
Before association, PMF does not apply. Sending deauth frames during
the authentication/association process itself prevents the client from
completing association.

Strategy:
- Monitor for authentication frames from clients to target AP
- Immediately inject deauth frames during the auth→assoc window
- Client cannot complete association → keeps retrying
- When rogue AP is up → client falls through to it

This is slower and less reliable than standard deauth but works
even on PMF-required networks.

---

## Part 4 — WPA2-Only Rogue AP

### What makes it different from existing evil twin

Existing T-Rex evil twin clones the target AP including its security.
For WPA3 downgrade, the rogue AP must advertise WPA2-PSK ONLY —
no SAE, no WPA3. This is what forces the downgrade.

Rogue AP configuration:
```
SSID:     same as target (exact match)
BSSID:    random or cloned from target
Channel:  same as target
Security: WPA2-PSK only (no SAE in AKM list)
PMF:      DISABLED (no MFPC, no MFPR bits set)
Password: any (we're capturing the handshake, not completing auth)
```

### Why victim connects to WPA2-only rogue AP

When victim's device scans after deauth and sees:
- Real AP: advertising SAE+PSK (transition mode)
- Rogue AP: advertising PSK only (WPA2)

The victim's WPA3 client will try SAE with the real AP first. Our
deauth prevents that. When it falls back and finds our rogue AP
advertising the same SSID with WPA2-PSK, it connects via WPA2.
This is the downgrade — the victim is now using WPA2 against our AP.

---

## Part 5 — EAPOL Handshake Capture

Once victim connects to rogue AP via WPA2, the 4-way EAPOL handshake
occurs. T-Rex captures this in monitor mode simultaneously.

We need messages 1+2 or messages 2+3 of the 4-way handshake minimum.
The complete 4-message handshake is ideal.

EAPOL frame identification:
```
Ethernet type: 0x888E
Frame filter: ethertype == 0x888E AND src == victim_MAC
```

Save captured handshake to SD in two formats:
- `/logs/handshake_<ssid>_<timestamp>.pcap` — raw PCAP
- `/logs/handshake_<ssid>_<timestamp>.hccapx` — Hashcat format

HCCAPX format allows direct import into Hashcat with:
```
hashcat -m 2500 handshake.hccapx wordlist.txt
```

---

## Part 6 — PMKID Capture (bonus, no client needed)

During the downgrade rogue AP phase, also attempt PMKID capture.
PMKID is available from message 1 of the 4-way handshake alone —
no need to wait for message 2 from the client.

Capturing the first two EAPOL handshake messages is
required for offline password cracking attacks.

But PMKID from message 1 alone is sufficient for hashcat mode 22000.
This means even if the victim doesn't complete the handshake
(disconnects or moves away), T-Rex still has crackable material.

Save as: `/logs/pmkid_<ssid>_<timestamp>.hc22000`

---

## Full Attack Flow

```
CMD> wpa3down

Step 1: Scan for transition mode APs
  → Shows only [TD] flagged APs
  → Displays PMF status for each

Step 2: User selects target AP

Step 3: Scan for clients on target AP
  → Monitor association frames
  → Build client list with MACs

Step 4: PMF probe test (if PMF? status)
  → Send test deauths
  → Confirm whether deauth works

Step 5: Start rogue AP (WPA2-only, same SSID)
  → Rogue AP running on T-Deck

Step 6: Deauth attack against selected clients
  → Standard deauth if PMF disabled/optional
  → Pre-association flood if PMF required

Step 7: Capture EAPOL handshake
  → Monitor mode captures 4-way handshake
  → PMKID extracted from message 1

Step 8: Save to SD and report
  → PCAP file
  → HCCAPX file for Hashcat
  → PMKID file for Hashcat mode 22000
  → Summary report
```

---

## UI Layout

### Phase 1 — Target Selection

```
┌─────────────────────────────────────┐
│ T-REX // WPA3 DOWNGRADE    [q] exit │
├─────────────────────────────────────┤
│ Transition mode APs only:           │
├─────────────────────────────────────┤
│ #  SSID            CH  RSSI  PMF    │
│ 1  OfficeNetwork   1   -61   OPT    │
│ 2  CafeGuest       6   -54   OFF    │
│ 3  HotelLobby      11  -72   OPT    │
├─────────────────────────────────────┤
│ [#] select target  [r] rescan       │
└─────────────────────────────────────┘
```

### Phase 2 — Attack Running

```
┌─────────────────────────────────────┐
│ T-REX // WPA3 DOWNGRADE    [q] exit │
├─────────────────────────────────────┤
│ Target: OfficeNetwork (AA:BB:CC:..) │
│ Rogue AP: OfficeNetwork [WPA2 ONLY] │
├─────────────────────────────────────┤
│ [+] PMF probe: NOT enforced ✓       │
│ [+] Rogue AP started on CH1         │
│ [+] Deauthing 3 clients...          │
│ [+] Client DD:EE:FF connected       │
│ [+] EAPOL msg 1 captured            │
│ [+] EAPOL msg 2 captured ✓          │
│ [+] PMKID extracted ✓               │
│ [+] Full handshake captured ✓       │
│                                     │
│ Saved: handshake_Office_1234.hccapx │
│ Saved: pmkid_Office_1234.hc22000    │
├─────────────────────────────────────┤
│ Crack: hashcat -m 2500 *.hccapx     │
└─────────────────────────────────────┘
```

---

## Data Structures

```cpp
#define WPA3DOWN_MAX_APS      20
#define WPA3DOWN_MAX_CLIENTS  16
#define WPA3DOWN_SSID_LEN     33

enum PmfStatus : uint8_t {
    PMF_DISABLED  = 0,   // MFPC=0, MFPR=0 → deauth works
    PMF_OPTIONAL  = 1,   // MFPC=1, MFPR=0 → test first
    PMF_REQUIRED  = 2,   // MFPC=1, MFPR=1 → deauth blocked
};

enum AuthMode : uint8_t {
    AUTH_WPA2_ONLY   = 0,
    AUTH_WPA3_ONLY   = 1,
    AUTH_TRANSITION  = 2,   // SAE + PSK both present → target
};

struct TransitionAP {
    uint8_t    bssid[6];
    char       ssid[WPA3DOWN_SSID_LEN];
    uint8_t    channel;
    int8_t     rssi;
    AuthMode   authMode;
    PmfStatus  pmf;
    bool       pmfProbed;      // have we sent test deauths?
    bool       pmfEnforced;    // result of probe test
    bool       active;
};

struct W3DClient {
    uint8_t  mac[6];
    uint8_t  apBssid[6];
    bool     deauthed;
    bool     connected;        // connected to our rogue AP
    bool     handshakeCaptured;
    bool     pmkidCaptured;
    bool     active;
};

struct EapolCapture {
    uint8_t  clientMac[6];
    uint8_t  apMac[6];
    uint8_t  anonce[32];       // from message 1
    uint8_t  snonce[32];       // from message 2
    uint8_t  mic[16];          // from message 2
    uint8_t  pmkid[16];        // from message 1 (if present)
    bool     msg1;
    bool     msg2;
    bool     msg3;
    bool     msg4;
    bool     pmkidValid;
};
```

---

## HCCAPX File Format

HCCAPX is the format Hashcat uses for WPA2 handshakes (mode 2500).
Each record is 392 bytes:

```cpp
struct hccapx {
    uint32_t signature;      // 0x4B455843 "KEXC"
    uint32_t version;        // 0x00000401
    uint8_t  message_pair;
    uint8_t  keyver;
    uint8_t  keymic[16];
    uint8_t  mac_ap[6];
    uint8_t  nonce_ap[32];
    uint8_t  mac_sta[6];
    uint8_t  nonce_sta[32];
    uint16_t eapol_len;
    uint8_t  eapol[256];
    uint8_t  essid_len;
    uint8_t  essid[32];
};
```

Write directly to SD as binary file.
User transfers to PC and runs:
```
hashcat -m 2500 capture.hccapx rockyou.txt
```

---

## Integration with Existing T-Rex Code

### scanwifi.cpp modifications
- Add RSN IE parsing in beacon callback
- Extract MFPC, MFPR bits from RSN Capabilities
- Extract AKM suite list — detect SAE (0x000FAC08) + PSK (0x000FAC02)
- Add PMF and AUTH columns to scan table display
- Add [TD], [PMF!], [PMF?] flags

### wifi_tools.cpp modifications
- Add `startRogueAP_WPA2Only()` function — variant of existing evil twin
  that forces WPA2-PSK only in AP config, no SAE
- Add `captureEAPOL()` function — filter for 0x888E ethertype
- Add `extractPMKID()` function — parse PMKID from EAPOL message 1
- Add `writeHCCAPX()` function — write handshake to SD in Hashcat format
- Add `writeHC22000()` function — write PMKID to SD in Hashcat format

### New files
- `t-rex-firmware/attacks/wpa3down.cpp/.h` — main command logic

### shell.cpp
- Register `wpa3down` and alias `w3d`

---

## Files to Create/Modify

- `t-rex-firmware/attacks/wpa3down.cpp/.h` — new command
- `t-rex-firmware/wifi_tools.cpp` — add EAPOL capture, PMKID, HCCAPX
- `t-rex-firmware/scanwifi.cpp` — add RSN IE parsing, PMF flags
- `t-rex-firmware/shell.cpp` — register command
- `README.md` — add to features, commands, roadmap

---

## README Updates

### Features → WiFi — update:
| WPA3 transition mode downgrade attack | 🔨 WIP |
| PMF detection from beacon RSN IE | 🔨 WIP |
| EAPOL handshake capture → HCCAPX export | 🔨 WIP |
| PMKID capture → HC22000 export | 🔨 WIP |

### Commands — add:
| `wpa3down` | `w3d` | WPA3 transition mode downgrade + handshake capture |

### Roadmap — update:
- [ ] WPA3 transition mode downgrade attack (wpa3down)
- [ ] PMF status detection in scanwifi
- [ ] EAPOL handshake capture → HCCAPX for Hashcat
- [ ] PMKID capture → HC22000 for Hashcat

### Credits — add:
- Dragonblood research (CVE-2019-9494 to CVE-2019-9499) by
  Mathy Vanhoef and Eyal Ronen — WPA3 design flaws research
- TrustedSec July 2024 WPA3 downgrade writeup
- RedLegg June 2025 eaphammer demonstration

---

## Limitations and Honest Notes

**What works:**
- Transition mode networks (SAE+PSK) where PMF is optional or disabled
- Most real-world enterprise networks confirmed vulnerable
- WPA2 clients on transition mode networks even if WPA3 clients are protected

**What does NOT work:**
- Pure WPA3 networks (SAE only) — no WPA2 fallback available
- Networks with PMF Required AND properly implemented → deauth blocked
- Forward secrecy: even with the password, previously captured WPA3 SAE
  traffic cannot be decrypted (only future WPA2 sessions are at risk)

**Real world prevalence:**
Transition mode is the default configuration on most routers shipping
in 2024-2026. The majority of "WPA3" networks in the wild are actually
running in transition mode and are vulnerable to this attack.

---

## Disclaimer

Add to README disclaimer:
```
wpa3down exploits published vulnerabilities (CVE-2019-9494 to
CVE-2019-9499) in WPA3 transition mode. Use only on networks you
own or have explicit written permission to test. Capturing network
credentials without authorization is illegal in most jurisdictions.
```
