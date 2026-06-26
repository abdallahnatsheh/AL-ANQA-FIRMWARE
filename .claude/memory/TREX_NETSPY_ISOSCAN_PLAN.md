# T-Rex — netspy & isoscan Feature Plan
# Based on AirSnitch (NDSS 2026) by Mathy Vanhoef
# Reference: https://github.com/vanhoefm/airsnitch

---

## Overview

Two new commands that work together to discover and attack devices
on networks with client isolation enabled:

1. `netspy` (`ns`) — passive + active device discovery that bypasses
   client isolation to reveal the full network topology
2. `isoscan` (`is`) — client isolation bypass attacks against discovered
   targets (GTK injection, gateway bouncing, port stealing)

**Typical attack flow:**
```
netspy → reveals all devices despite client isolation
       ↓
isoscan → selects target from netspy results
        → escalates from packet injection to full traffic interception
```

---

## Prerequisites

Both commands require T-Deck to be connected to the target network.
Use existing `connectwifi` command first.

---

## Feature 1 — `netspy`

### Command
```
CMD> netspy
```
Alias: `ns`

### What it does
Discovers all devices on the network even when client isolation is
enabled. Standard ARP scan (`netdiscover`) fails on isolated networks
because the AP blocks unicast ARP between clients. `netspy` bypasses
this using multiple passive and active techniques that client isolation
does not block.

---

### Discovery Methods (run in parallel)

**Method 1 — DHCP Snooping (passive)**
Monitor broadcast DHCP traffic. Devices requesting or renewing IPs
broadcast on the network — client isolation never blocks broadcasts.
Reveals: MAC address, requested hostname, assigned IP, device vendor.

DHCP packet types to capture:
- DHCPDISCOVER — device looking for IP (reveals MAC + hostname)
- DHCPREQUEST — device renewing IP (reveals MAC + current IP)
- DHCPACK — server response (reveals assigned IP + lease time)

All captured passively using promiscuous mode — zero packets sent.

**Method 2 — mDNS Passive Sniffing (passive)**
Devices constantly broadcast mDNS (Multicast DNS) on 224.0.0.251:5353.
Client isolation almost never blocks multicast traffic.
Reveals: device hostname (.local), service types, OS hints.

Common mDNS service types to watch:
- `_airplay._tcp` → Apple TV / AirPlay device
- `_raop._tcp` → AirPlay audio (iPhone, Mac)
- `_googlecast._tcp` → Chromecast / Google Home
- `_ipp._tcp` → Network printer
- `_smb._tcp` → Windows file sharing
- `_ssh._tcp` → SSH server
- `_http._tcp` → Web server
- `_spotify-connect._tcp` → Spotify device

**Method 3 — SSDP Passive Sniffing (passive)**
UPnP/SSDP devices broadcast on 239.255.255.250:1900.
Reveals: device type, manufacturer, model, services.
Common on smart TVs, routers, game consoles, smart home devices.

**Method 4 — IPv6 Neighbor Discovery (passive)**
Devices send IPv6 multicast Neighbor Solicitation/Advertisement.
Reveals: IPv6 link-local address, MAC address.
Almost never filtered by client isolation.

**Method 5 — Beacon / Association Frame Sniffing (passive)**
While connected, monitor management frames.
When devices connect/reconnect to AP, their MAC appears in
association request frames. Client isolation only applies to
data frames, not management frames.

**Method 6 — GTK Broadcast ARP (active)**
After passively collecting devices, use the shared GTK group key
to send a properly-encrypted broadcast ARP request:
```
Dot11(dst=ff:ff:ff:ff:ff:ff) / ARP(op=who-has, pdst=255.255.255.255)
```
Encrypted with GTK → AP forwards it to all clients → clients that
respond confirm their IP and MAC. This is the AirSnitch GTK technique
applied to discovery instead of attack.

Devices that respond to GTK broadcast are marked as
`GTK-REACHABLE` → these are confirmed targets for isoscan.

**Method 7 — OUI Vendor Identification**
For every discovered MAC address, look up the OUI (first 3 bytes)
against a built-in vendor table to identify device manufacturer.
Combined with mDNS/SSDP data → accurate device type identification.

Built-in OUI table should cover:
- Apple (iPhones, Macs, iPads)
- Samsung (Android phones, TVs)
- Google (Chromecasts, Pixels)
- Amazon (Echo, Fire TV)
- Microsoft (Surface, Xbox)
- Raspberry Pi Foundation
- Common router manufacturers

---

### Data Structures

```cpp
#define NETSPY_MAX_DEVICES   32
#define NETSPY_HOSTNAME_LEN  32
#define NETSPY_SERVICE_LEN   16
#define NETSPY_MAX_SERVICES   8

enum DiscoveryMethod : uint8_t {
    METHOD_DHCP     = 0x01,
    METHOD_MDNS     = 0x02,
    METHOD_SSDP     = 0x04,
    METHOD_IPV6_ND  = 0x08,
    METHOD_BEACON   = 0x10,
    METHOD_GTK_ARP  = 0x20,
};

struct NetSpyDevice {
    uint8_t  mac[6];
    uint32_t ip;               // IPv4, 0 if unknown
    uint8_t  ipv6[16];         // link-local IPv6, zeroed if unknown
    char     hostname[NETSPY_HOSTNAME_LEN];
    char     vendor[16];       // OUI vendor name
    char     services[NETSPY_MAX_SERVICES][NETSPY_SERVICE_LEN];
    uint8_t  serviceCount;
    uint8_t  discoveredBy;     // bitmask of DiscoveryMethod
    bool     gtkReachable;     // responded to GTK broadcast ARP
    uint32_t firstSeen;
    uint32_t lastSeen;
    bool     active;
};

NetSpyDevice devices[NETSPY_MAX_DEVICES];
int          deviceCount;
```

---

### UI Layout

```
┌─────────────────────────────────────┐
│ T-REX // NET SPY           [q] exit │  ← RED header
├─────────────────────────────────────┤
│ DHCP:✓ mDNS:✓ SSDP:✓ GTK:✓  t:03:21│  ← active methods + elapsed
├─────────────────────────────────────┤
│ #  IP            VENDOR    GTK  HOW │
│ 1  192.168.1.5   Apple     ✓   DM  │  ← GTK reachable (red = target)
│ 2  192.168.1.8   Samsung   ✓   DM  │
│ 3  192.168.1.12  Dell      ✗   M   │  ← not GTK reachable (yellow)
│ 4  192.168.1.3   Canon     ✗   S   │
│ 5  192.168.1.21  Amazon    ✓   DS  │
│                                     │
│ HOW: D=DHCP M=mDNS S=SSDP G=GTK    │
├─────────────────────────────────────┤
│ Devices: 5  GTK targets: 3          │
└─────────────────────────────────────┘
```

Press `i` on a device → show full detail (hostname, services, MACs).
Press `a` → export all to isoscan target list.
Press `s` → save to SD `/logs/netspy.log`.

---

### Key Bindings
- `l` / `a` → next/prev page
- `i` → device detail view
- `g` → trigger GTK broadcast ARP now
- `a` → send device list to isoscan
- `s` → save log to SD
- `q` → quit

### SD Logging
Save to `/logs/netspy.log`:
```
[timestamp] MAC | IP | VENDOR | HOSTNAME | SERVICES | GTK_REACHABLE | METHODS
```

---

## Feature 2 — `isoscan`

### Command
```
CMD> isoscan [#index]
```
Alias: `is`

If index provided → attack that device from netspy results directly.
If no index → show target picker from last netspy scan.

### What it does
Runs client isolation bypass attacks against a specific target.
Escalates from reconnaissance to full traffic interception.
Based on AirSnitch NDSS 2026 techniques.

---

### Attack Modules

---

#### Module 1 — GTK Check (`isoscan gtk`)

**What:** Verify the network shares the same GTK with all clients.
If GTK is shared → attacker can encrypt/inject packets to any client.

**How:**
Connect to network → capture 4-way handshake → extract GTK group key.
The GTK is received automatically during normal WiFi association.

**Output:**
```
[+] GTK obtained: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
[+] GTK is SHARED — network is vulnerable to GTK injection
```
or
```
[-] GTK appears randomized — GTK injection likely won't work
```

**Impact:** Confirms whether attacks 2-4 below are feasible.

---

#### Module 2 — GTK Packet Injection (`isoscan inject`)

**What:** Inject malicious packets to victim using shared GTK.
Most impactful: inject ICMPv6 Router Advertisement to poison
victim's DNS server → all victim DNS queries come to T-Deck.

**How:**
Encrypt packet with GTK, send as broadcast WiFi frame:
```
Dot11(dst=ff:ff:ff:ff:ff:ff, src=AP_MAC)
  / IPv6 / ICMPv6RouterAdvertisement(dns=T-Deck-IP)
```
Victim OS accepts it (properly GTK-encrypted broadcast).
Victim now uses T-Deck as DNS server.
T-Deck logs all DNS queries to SD card.

**Payload options:**
```
isoscan inject dns <victim_ip>    → ICMPv6 RA DNS poisoning
isoscan inject arp <victim_ip>    → ARP cache poisoning
isoscan inject raw <victim_ip>    → raw packet from SD card file
```

**DNS capture after injection:**
Once victim's DNS is poisoned, start UDP listener on port 53.
Log all queries to `/logs/isoscan_dns.log`:
```
[timestamp] VICTIM_IP | QUERY_TYPE | DOMAIN
```

**Impact:** Silent, zero disruption, full DNS visibility on victim.
Works even when client isolation blocks all other methods.

---

#### Module 3 — Gateway Bouncing (`isoscan bounce`)

**What:** Send packets to victim via gateway at IP layer.
Client isolation only checks MAC layer — gateway routes at IP layer.

**How:**
```
Ethernet(src=T-Deck, dst=gateway_MAC)
  / IP(src=T-Deck, dst=victim_IP)
  / ICMP echo request
```
If victim replies → gateway bouncing works → T-Deck can reach victim.

**Output:**
```
[+] Gateway bouncing SUCCESSFUL — victim replied in 12ms
[+] Client isolation bypass confirmed at IP layer
```
or
```
[-] No reply — gateway bouncing blocked or victim filtered ICMP
```

**Impact:** T-Deck can now communicate directly with victim despite
client isolation. Enables follow-on attacks (port scan victim, etc.).

---

#### Module 4 — Broadcast Reflection (`isoscan bcast`)

**What:** Test if AP forwards broadcast frames with unicast IP destination.
Send broadcast WiFi frame containing victim's IP as destination.

**How:**
```
Dot11(FCfield="to-DS", addr1=T-Deck, addr2=AP, addr3=ff:ff:ff:ff:ff:ff)
  / IP(dst=victim_IP, src=T-Deck)
```
If victim responds → AP is forwarding broadcast frames with unicast IPs
despite client isolation → GTK injection will also work.

**Output:**
```
[+] Broadcast reflection ALLOWED — victim received and replied
[-] Broadcast reflection blocked
```

---

#### Module 5 — Downlink Port Stealing (`isoscan portdown`)

**What:** Intercept traffic SENT TO the victim (downlink).
Spoof victim's MAC → network routes victim's incoming traffic to T-Deck.

**How:**
1. Note current T-Deck MAC
2. Disconnect from network
3. Change MAC to victim's MAC: `esp_wifi_set_mac(victim_mac)`
4. Reconnect to a different AP/BSSID on same network
5. Inject bogus IPv4 packets spoofing victim's MAC
6. Network gets confused → victim's downlink traffic routes to T-Deck
7. Capture traffic, log to SD as PCAP

**Output:**
```
[+] Downlink port stealing SUCCESSFUL
[+] Capturing victim's incoming traffic → /logs/isoscan_portdown.pcap
```

**Restore:** On exit, restore original T-Deck MAC and reconnect normally.

---

#### Module 6 — Uplink Port Stealing (`isoscan portup`)

**What:** Intercept traffic SENT BY the victim (uplink).
Spoof gateway's MAC → when victim sends to gateway, traffic comes to T-Deck.

**How:**
1. Disconnect from network
2. Change MAC to gateway's MAC: `esp_wifi_set_mac(gateway_mac)`
3. Reconnect to network (same or different BSSID)
4. Inject bogus IPv4 packets spoofing gateway's MAC
5. Network routes victim's outgoing traffic to T-Deck
6. Capture traffic, log to SD as PCAP

**Output:**
```
[+] Uplink port stealing SUCCESSFUL
[+] Capturing victim's outgoing traffic → /logs/isoscan_portup.pcap
```

---

#### Module 7 — Full Auto (`isoscan auto`)

Run all modules in sequence against a target, report which succeed:

```
CMD> isoscan auto 1    (target device #1 from netspy)

[1/5] GTK check...          ✓ GTK shared
[2/5] GTK injection test... ✓ victim received injected packet
[3/5] Gateway bouncing...   ✓ victim reachable via gateway
[4/5] Broadcast reflect...  ✓ AP forwarding broadcast frames
[5/5] Port stealing...      ✓ downlink intercepted

RESULT: Network fully compromised
BEST ATTACK: GTK DNS injection (silent, no disruption)
```

---

### UI Layout — Target Picker

```
┌─────────────────────────────────────┐
│ T-REX // ISO SCAN          [q] exit │  ← RED header
├─────────────────────────────────────┤
│ Select target from last netspy scan:│
├─────────────────────────────────────┤
│ #  IP            VENDOR    GTK      │
│ 1  192.168.1.5   Apple     ✓        │  ← GTK reachable targets in RED
│ 2  192.168.1.8   Samsung   ✓        │
│ 3  192.168.1.21  Amazon    ✓        │
├─────────────────────────────────────┤
│ [1-3] select  [a] auto-all  [q] quit│
└─────────────────────────────────────┘
```

### UI Layout — Attack Running

```
┌─────────────────────────────────────┐
│ T-REX // ISO SCAN          [q] exit │
├─────────────────────────────────────┤
│ Target: 192.168.1.5 (Apple)         │
│ Attack: GTK DNS Injection           │
├─────────────────────────────────────┤
│                                     │
│  [+] GTK obtained                   │
│  [+] Injecting ICMPv6 RA...         │
│  [+] Victim DNS poisoned ✓          │
│                                     │
│  Capturing DNS queries:             │
│  [10:23:01] google.com A            │
│  [10:23:03] apple.com A             │
│  [10:23:07] instagram.com A         │
│  [10:23:12] whatsapp.net A          │
│                                     │
├─────────────────────────────────────┤
│ Queries: 4  Saved to SD  [q] stop  │
└─────────────────────────────────────┘
```

---

### Data Structures

```cpp
enum IsoAttack : uint8_t {
    ATTACK_GTK_CHECK   = 0,
    ATTACK_GTK_INJECT  = 1,
    ATTACK_BOUNCE      = 2,
    ATTACK_BCAST       = 3,
    ATTACK_PORT_DOWN   = 4,
    ATTACK_PORT_UP     = 5,
    ATTACK_AUTO        = 6,
};

struct IsoResult {
    IsoAttack attack;
    bool      success;
    char      detail[64];
};

#define ISO_MAX_RESULTS  8
IsoResult results[ISO_MAX_RESULTS];
int       resultCount;

// GTK key storage
uint8_t  gtk[32];
uint8_t  gtkLen;
bool     gtkObtained;

// Original MAC for restore after port stealing
uint8_t  originalMac[6];
```

---

### MAC Restore Safety

Port stealing attacks change T-Deck's MAC address. This MUST be
restored on exit — even if user presses `q` mid-attack.

```cpp
// On isoscan start: save original MAC
esp_wifi_get_mac(WIFI_IF_STA, originalMac);

// On any exit path (q, error, crash):
esp_wifi_set_mac(WIFI_IF_STA, originalMac);
WiFi.disconnect();
// reconnect with original MAC
```

Use `atexit()` or a cleanup flag checked in the main loop to ensure
MAC is always restored. A T-Deck stuck with a spoofed MAC would be
confusing and hard to debug.

---

## Radio Conflict Rules

```
netspy running:
  → promiscuous ON
  → cannot run: deauth, wifimon, eviltwin, trackme, isoscan

isoscan running:
  → active packet injection
  → cannot run: netspy, deauth, wifimon, eviltwin, trackme

Both are foreground commands — conflicts auto-resolve on exit.
Check WiFi connection state on start, abort if conflicting mode active.
```

---

## Files to Create

- `t-rex-firmware/attacks/netspy.cpp/.h`
- `t-rex-firmware/attacks/isoscan.cpp/.h`
- `t-rex-firmware/attacks/iso_common.cpp/.h` — shared GTK, MAC spoof utils

## Files to Modify

- `t-rex-firmware/shell.cpp` — register netspy/ns and isoscan/is
- `README.md` — add both to features, commands, roadmap

---

## README Updates

### Features — add new section 🕵️ Network Intelligence:
| Feature | Status |
|---------|--------|
| Device discovery bypassing client isolation | 🔨 WIP |
| DHCP / mDNS / SSDP passive sniffing | 🔨 WIP |
| GTK broadcast ARP discovery | 🔨 WIP |
| GTK shared key detection | 🔨 WIP |
| GTK packet injection + DNS poisoning | 🔨 WIP |
| Gateway bouncing bypass | 🔨 WIP |
| Downlink / uplink port stealing | 🔨 WIP |

### Commands table — add:
| `netspy` | `ns` | Discover devices bypassing client isolation |
| `isoscan` | `is` | Client isolation bypass attacks (AirSnitch) |

### Roadmap — add:
- [ ] netspy — passive device discovery bypassing client isolation
- [ ] isoscan — GTK injection, gateway bounce, port stealing (AirSnitch)

### Credits — add:
- [AirSnitch](https://github.com/vanhoefm/airsnitch) by Mathy Vanhoef
  (NDSS 2026) — client isolation bypass techniques. No code used;
  attack techniques implemented independently from published research.

---

## License Note

AirSnitch has no license file — all rights reserved.
However the attack techniques are fully documented in the
published NDSS 2026 paper (public academic research).
T-Rex implements these techniques independently from scratch
based on the paper — no AirSnitch code is used or adapted.
Credit the paper and repo in comments and README only.

---

## Disclaimer Note for README

Add to disclaimer section:
```
netspy and isoscan implement techniques from academic research
(AirSnitch, NDSS 2026). Use only on networks you own or have
explicit written permission to test. These attacks can intercept
other users' traffic — unauthorized use is illegal in most
jurisdictions.
```

---

## Coding Standards (same as rest of T-Rex)

- No dynamic allocation
- No STL containers
- Fixed-size buffers
- Follow RED header UI pattern
- Consistent nav keys (l/a/q/s)
- All display via dm. methods
- All input via inputHandler.getKeyboardInput()
- MAC MUST be restored on any exit path from isoscan
- Always check WiFi connection before starting either command
- Log all captured data to SD when available
