// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "man_pages.h"
#include "lockscreen_manager.h"
#include <cctype>

extern InputHandling inputHandler;

// ── Man page data ─────────────────────────────────────────────────────────────

static const int MAN_VISIBLE = 9;  // content lines visible at once

struct ManEntry {
    const char* cmd;
    const char* shortName;
    const char* lines[48]; // nullptr-terminated, max 47 content lines
};

static const ManEntry PAGES[] = {

    { "help", "hlp", {
        "SYNTAX   help [command]",
        "",
        "ABOUT    List commands by category.",
        "         help <cmd> for single command detail.",
        "",
        "KEYS     [l] next  [a] prev  [q] quit",
        "NOTE     man pages: tpad UP/DN scrolls.",
        "         [a]/[l] change page.",
        "EXAMPLE  help wpasniff",
        nullptr
    }},

    { "info", "inf", {
        "SYNTAX   info",
        "",
        "ABOUT    Device info — 3 pages:",
        "         1. Chip/RAM/flash   2. MACs",
        "         3. LoRa/GPS/battery",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        nullptr
    }},

    { "clear", "clr", {
        "SYNTAX   clear",
        "",
        "ABOUT    Clear screen and reset prompt.",
        nullptr
    }},

    { "MATRIX", "matrix", {
        "SYNTAX   MATRIX",
        "",
        "ABOUT    Matrix digital rain animation.",
        "KEYS     [q] exit",
        nullptr
    }},

    { "lock", "lk", {
        "SYNTAX   lock",
        "         lock new | update | clean | wipe",
        "         lock boot on|off",
        "         lock timeout <s>  |  lock status",
        "",
        "ABOUT    No PIN: press Space x3 to unlock.",
        "         PIN: type password then Enter.",
        "",
        "LOCK     Run 'lock' or hold trackpad 3s.",
        "BOOT     lock boot on  = lock at power-on",
        "TIMEOUT  lock timeout <s>  (0 = off)",
        "RECOVER  Forgot PIN? Remove SD + reboot",
        "         (PIN lives on the card). Or on",
        "         a PC add 'reset=1' to",
        "         /config/lockscreen.conf, reboot.",
        nullptr
    }},

    { "tz", "tz", {
        "SYNTAX   tz               — pick from list",
        "         tz status        — show current TZ + time",
        "         tz +3 | tz -5   — set UTC offset directly",
        "         tz +3:30        — half-hour offset",
        "",
        "ABOUT    Scrollable list of 30+ common timezones.",
        "         UP/DN to move, Enter to select, q to quit.",
        "         WiFi: NTP syncs local time automatically.",
        "         No WiFi: GPS used as source (Plus only).",
        "         Saved to /config/clock.conf on SD.",
        nullptr
    }},

    { "pwrsave", "psv", {
        "SYNTAX   pwrsave on|off|status",
        "         pwrsave set <option> <value>",
        "         pwrsave save|reset",
        "",
        "ABOUT    Dim + screen-off on inactivity, battery-aware dim.",
        "         Config saved to /config/pwrsave.conf.",
        "",
        "SET      timeout <s>           — inactivity dim delay",
        "         dimto <0-255>         — dim brightness level",
        "         fullto <0-255>        — full brightness level",
        "         screenoff <s>         — screen-off delay",
        "         screenoffmode on|off  — enable screen-off",
        "         batterymode on|off    — battery-aware dim",
        "         batterythreshold <%>  — dim below this %",
        "         batterydim <0-255>     — battery dim level",
        "",
        "EXAMPLE  pwrsave set timeout 60",
        "         pwrsave set dimto 40",
        "         pwrsave set batterymode on",
        "         pwrsave set batterythreshold 20",
        nullptr
    }},

    { "sleep", "slp", {
        "SYNTAX   sleep   (alias: slp)",
        "",
        "ABOUT    Enter ESP32-S3 deep sleep (~240uA).",
        "         Fades backlight, sleeps the panel,",
        "         powers peripherals down.",
        "",
        "WAKE     Click the trackball (center button).",
        "         Wake = full reboot to a fresh prompt;",
        "         RAM-only state (unsaved data) is lost.",
        "",
        "NOTE     Manual only — never auto-triggers.",
        "         The keyboard CANNOT wake it (its INT",
        "         line is not an RTC pin). pwrsave's",
        "         timeout only dims/blanks the backlight.",
        nullptr
    }},

    { "volume", "vol", {
        "SYNTAX   vol [0-100|up|down|off]",
        "",
        "ABOUT    Master audio volume (NES emulator,",
        "         future music / voice recorder).",
        "         vol alone shows current level.",
        "",
        "OPTIONS  up  +10%   down  -10%",
        "         off mute   0-100 exact value",
        "PERSIST  Saved to /config/vol.conf on SD.",
        "         Restored on every boot.",
        "NOTE     Does not affect notification vol.",
        "         Use: nf vol <n> for that.",
        nullptr
    }},

    { "notif", "nf", {
        "SYNTAX   notif [on|off|status]",
        "         notif vol <0-100>",
        "         notif test [level]",
        "         notif <level> on|off",
        "         notif <level> file <path>",
        "",
        "LEVELS   alert  warning  success  info  ping",
        "ABOUT    Per-level audio notifications.",
        "         Custom WAV (22050Hz mono 16-bit) from",
        "         /config/notification/*.wav",
        "         Config saved to /config/notif.conf",
        "",
        "EXAMPLE  notif test          - sound picker",
        "         notif test alert    - play one",
        "         notif alert file alert.wav",
        nullptr
    }},

    { "show", "sh", {
        "SYNTAX   show <wifi|ble|hosts>",
        "",
        "ABOUT    Re-display the last scan result",
        "         without running a new scan.",
        "",
        "OPTIONS  wifi  — last scanwifi table",
        "         ble   — last scanblue table",
        "         hosts — last netdiscover table",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        "NOTE     'No scan data' if not run yet.",
        nullptr
    }},

    { "scanwifi", "sw", {
        "SYNTAX   sw [on|off]",
        "",
        "ABOUT    WiFi manager. No arg = interactive",
        "         scan list (connect/disconnect/",
        "         forget). on|off = radio power.",
        "         Shows current connection + SSID,",
        "         RSSI, security. Hidden = ~name.",
        "",
        "SEC      OPEN/WEP/WPA/WPA2/WPA3, and",
        "         WPA3/TD (yellow) = WPA2+WPA3",
        "         transition, downgradeable (w3d).",
        "",
        "KEYS     trkbl=sel  click/ent=connect",
        "         [d]isc [f]orget [o]n/off",
        "         [u] rescan  [q] quit",
        "NOTE     Index # used by cw da et hs ws.",
        nullptr
    }},

    { "connectwifi", "cw", {
        "SYNTAX   cw <index>",
        "",
        "ABOUT    Connect to network from last scan.",
        "         Password saved in NVS — not re-asked.",
        "",
        "EXAMPLE  sw  then  cw 2",
        nullptr
    }},

    { "wifipass", "wp", {
        "SYNTAX   wifipass            (alias: wp)",
        "         wifipass export",
        "         wifipass clear",
        "",
        "ABOUT    Manage saved WiFi credentials.",
        "         No arg: browse saved passwords",
        "         (SD wpa_supplicant.conf, NVS fallback).",
        "",
        "EXPORT   Copy NVS networks to SD card",
        "         (/wpa_supplicant.conf, skips dups,",
        "         Linux-compatible).",
        "CLEAR    Erase all saved passwords. Does not",
        "         disconnect the active session.",
        "",
        "KEYS     [a]prev [l]next [q]quit",
        "FILES    /wpa_supplicant.conf",
        nullptr
    }},

    { "wifimon", "wm", {
        "SYNTAX   wm [channel]",
        "",
        "ABOUT    802.11 monitor: two views, PCAP,",
        "         probe logger. 0=hop, 1-13=fixed.",
        "",
        "VIEWS    [v] toggle Nets <-> Clients",
        "  Nets:  BSSID CH RSSI Clients SSID",
        "  Cli:   MAC Vendor Type RSSI AP",
        "",
        "KEYS     [h] ch-hop  [1-9] fix ch",
        "         [s] PCAP    [p] probe log",
        "         [d] deauth selected client",
        "         [^v] move cursor  [q] quit",
        "",
        "FILES    /apps/wifimon/NNN.cap",
        "         /apps/wifimon/probes.csv",
        nullptr
    }},

    { "deauth", "da", {
        "SYNTAX   da <bssid|#> [ch] [client]",
        "",
        "ABOUT    802.11 deauth to kick clients.",
        "         Broadcast or targeted one client.",
        "",
        "EXAMPLE  da 2",
        "         da AA:BB:CC:DD:EE:FF 6",
        "         da 2 6 CC:DD:EE:FF:00:11",
        "KEYS     [q] stop",
        nullptr
    }},

    { "eviltwin", "et", {
        "SYNTAX   et [ssid]",
        "",
        "ABOUT    Clone AP + captive portal.",
        "         Open=clone MAC. WPA=random MAC.",
        "         Deauth pauses on client connect.",
        "",
        "KEYS     [p] portal page  [c] creds",
        "         [s] save  [q] quit",
        "FILES    /apps/eviltwin/portal/*.html",
        "         /apps/eviltwin/creds.csv",
        nullptr
    }},

    { "hiddenssid", "hs", {
        "SYNTAX   hs <idx|bssid> [ch] [silent]",
        "",
        "ABOUT    Reveal hidden SSID via deauth +",
        "         probe-response sniff. Saved to SD,",
        "         shown as ~name in scanwifi table.",
        "",
        "EXAMPLE  hs 3",
        "         hs AA:BB:CC:DD:EE:FF 11 silent",
        "KEYS     [q] stop",
        nullptr
    }},

    { "macchanger", "mc", {
        "SYNTAX   mc [on|off|random|set <mac>]",
        "         mc restore on|off",
        "         mc target wifi|bt|both",
        "",
        "ABOUT    Spoof WiFi/BLE MAC. Auto-random",
        "         on scan/connect when enabled.",
        "         Config: /config/macchanger.conf",
        "",
        "EXAMPLE  mc on  |  mc random  |  mc off",
        "         mc set 02:AB:CD:EF:12:34",
        "         mc target bt",
        nullptr
    }},

    { "wpasniff", "ws", {
        "SYNTAX   ws <idx|bssid> [ch]",
        "",
        "ABOUT    Capture WPA2 handshake + crack.",
        "         EAPOL sniff + deauth every 4s.",
        "         Needs M1+M2 (requires a client).",
        "",
        "STEPS    1. sw  2. ws <idx>  3. wait",
        "         4. c  to crack on-device",
        "KEYS     [c] crack  [q] quit",
        "FILES    /apps/wpasniff/wordlist.txt  (SD wordlist)",
        "         /apps/wpasniff/<BSSID>.cap  (pcap)",
        "         /apps/wpasniff/cracked.csv  (results)",
        "NOTE     Built-in 100 pwds if no SD card.",
        nullptr
    }},

    { "pmkid", "pm", {
        "SYNTAX   pm <idx|bssid> [ch]",
        "",
        "ABOUT    PMKID capture + on-device crack.",
        "         No client needed — sniffs EAPOL M1",
        "         passively. Stealthier than ws.",
        "",
        "STEPS    1. sw  2. pm <idx>  3. wait",
        "         4. c  to crack on-device",
        "CRACK    PBKDF2 -> HMAC-SHA1-128 vs PMKID",
        "KEYS     [c] crack  [q] quit",
        "FILES    /apps/pmkid/<BSSID>.cap  (pcap)",
        "         /apps/pmkid/cracked.csv  (results)",
        "NOTE     Not all routers include PMKID.",
        "         Fall back to ws if no PMKID seen.",
        nullptr
    }},

    { "wpa3down", "w3d", {
        "SYNTAX   w3d [idx] [victim-mac]",
        "",
        "ABOUT    [EXP] WPA3 transition-mode",
        "         downgrade. Deauth victim off the",
        "         real AP, run a WPA2-ONLY rogue AP",
        "         (same SSID) so it reconnects over",
        "         WPA2 -> crackable handshake.",
        "",
        "STEPS    1. sw   2. w3d  (pick a WPA3/TD)",
        "         3. wait for M2   4. cc to crack",
        "VICTIM   Add a client MAC for DIRECTED",
        "         deauth (broadcast is often ignored)",
        "         e.g. w3d 3 AA:BB:CC:DD:EE:FF",
        "TARGET   Only WPA3/TD (transition) APs are",
        "         downgradeable. idx = sw scan index.",
        "         Re-scan sw first (BSSIDs rotate).",
        "KEYS     trkbl/ent pick  [q] stop",
        "FILES    /apps/wpa3down/<ssid>.cap",
        "LIMITS   PMF-required APs won't drop (deauth",
        "         blocked). Pure WPA3 not downgradeable.",
        "         Dragonblood CVE-2019-9494..9499.",
        nullptr
    }},

    { "wguard", "wg", {
        "SYNTAX   wg <idx|bssid> [ch]     interactive",
        "         wg <idx|bssid> [ch] bg  background",
        "         wg stop  |  wg view",
        "",
        "ABOUT    Passive WiFi IDS — monitors one AP",
        "         for known attacks in real time.",
        "",
        "STEPS    1. sw  2. wg <idx>  3. monitor",
        "         bg: wg <idx> bg  then  wg view",
        "DETECTS  Deauth storm, bcast deauth, evil twin",
        "         handshake harvest, BSSID clone,",
        "         beacon/auth/probe flood, Karma",
        "KEYS     [s] save  [q] quit",
        "FILES    /apps/wguard/NNN.csv",
        "NOTE     bg blocks WiFi cmds; wg stop first.",
        nullptr
    }},

    { "beaconflood", "bf", {
        "SYNTAX   bf",
        "         bf list | rickroll | seq <base>",
        "         bf file [path] | bf clone",
        "",
        "ABOUT    Inject fake 802.11 beacons. ~90/s.",
        "         list=funny SSIDs  rick=rickroll",
        "         seq=base+N  file=SD  clone=real AP",
        "         clone needs sw first.",
        "",
        "KEYS     [q] stop",
        "NOTE     Cannot run with wguard at same time.",
        nullptr
    }},

    { "karma", "km", {
        "SYNTAX   km            (harvest + list)",
        "         km auto             (hands-free)",
        "         km hs <ssid> [ch]   (WPA2 cap)",
        "         km portal <ssid>    (open portal)",
        "",
        "ABOUT    Harvest probe requests, fingerprint",
        "         devices by PNL (beats MAC random),",
        "         then bait a network they want:",
        "         [h]=WPA2 half-handshake -> .cap",
        "         [p]=open AP + captive portal -> creds",
        "",
        "VIEWS    [v] HARV (nets) <-> DEVS (devices)",
        "         tpad=select  [a]/[l]=page",
        "KEYS     [h] handshake  [p] portal",
        "         [s] save tables  [c] clear  [q] stop",
        "WPA2     [c] crack captured M2 on-device",
        "         (SD wordlist or built-in)",
        "PORTAL   [p] picks built-in (Generic/",
        "         Google/Router) or SD .html page",
        "FILES    /apps/karma/<ssid>.cap  (handshake)",
        "         /apps/karma/creds.csv   (portal)",
        "         /apps/karma/cracked.csv (results)",
        "         /apps/karma/wordlist.txt (crack)",
        "         /apps/karma/portal/*.html (pages)",
        "AUTO     km auto = harvest + reactive bait",
        "         (follows live probes); M2 -> .cap +",
        "         connects.csv. km auto deauth = also",
        "         deauth nearby APs. [v]caps [q]stop.",
        "NOTE     .cap also cracks offline (aircrack/",
        "         hashcat) or on-device via cc.",
        "         Works with no SD/GPS.",
        nullptr
    }},

    { "wardrive", "wd", {
        "SYNTAX   wardrive   (T-Deck Plus only)",
        "",
        "ABOUT    Continuous WiFi scan + GPS fix ->",
        "         WiGLE WiFi-1.4 CSV. Logs each AP",
        "         once per session with coords.",
        "",
        "GPS      Starts the GPS task if not running.",
        "         Waits for the first fix (radio idle)",
        "         THEN scans + logs. Cold fix ~4 min",
        "         outdoors. No fix = nothing logged.",
        "",
        "FILES    /apps/wardrive/NNN.csv (never over-",
        "         written). Created on the FIRST AP -",
        "         empty runs leave no file. wigle.net.",
        "KEYS     q stop  (GPS stays on)",
        nullptr
    }},
    { "crack", "cc", {
        "SYNTAX   cc                  (pick cap+list)",
        "         cc <cap> [wordlist]",
        "         cc <cap> <dir>      (all *.txt)",
        "",
        "ABOUT    Offline WPA/WPA2 crack of a .cap:",
        "         4-way handshake (M1+M2) or PMKID.",
        "         Reads karma/ws/pm + external caps.",
        "",
        "PATHS    relative to your cd dir - after",
        "         cd-ing in, just pass filenames.",
        "         no cap arg -> pick a .cap here;",
        "         dir arg    -> pick a .cap inside.",
        "WLIST    no arg -> built-in / ALL *.txt /",
        "         pick one; or pass a file or dir.",
        "         built-in (100) always tried last.",
        "KEYS     1-8 pick  a/l page  q cancel/stop",
        "FILES    /apps/capcrack/cracked.csv",
        "NOTE     needs an ESSID (beacon) in the cap",
        "         to derive the PMK. SD only.",
        nullptr
    }},

    { "netdiscover", "nd", {
        "SYNTAX   netdiscover",
        "",
        "ABOUT    ARP scan local /24 subnet.",
        "         Shows IP, MAC, hostname.",
        "         Requires active WiFi connection.",
        "",
        "KEYS     [l]/[a] pages  [u] rescan  [q]",
        "NOTE     Use index # in ps and ts.",
        nullptr
    }},

    { "netspy", "ns", {
        "SYNTAX   netspy           recon table",
        "         ns gtk           show group key",
        "         ns dump          gWpaSm -> SD",
        "",
        "ABOUT    [EXP] Find devices on a WiFi with",
        "         CLIENT ISOLATION, where nd (ARP",
        "         scan) sees only the gateway.",
        "         PASSIVE - never transmits a frame.",
        "",
        "HOW      Sniffs the broadcast/multicast the",
        "         AP relays to all clients (your HW",
        "         decrypts them). Parses ARP/IPv4 +",
        "         DHCP/mDNS/SSDP for names+services.",
        "",
        "KEYS     tpad U/D sel   Enter detail",
        "         p ping   o portscan (sel dev)",
        "         [s] save [c] clr [l]/[a] pg  q",
        "FLAGS    A=ARP I=IPv4 D=DHCP M=mDNS S=SSDP",
        "CLI      ps ns# / pg ns# target a row #",
        "FILES    /apps/netspy/NNN.csv",
        "NOTE     Connect first (cw). AirSnitch",
        "         technique. Own networks only.",
        nullptr
    }},

    { "isoscan", "is", {
        "SYNTAX   is                 pick victim+attack",
        "         is ns<#> <attack>  target a netspy row",
        "         is cctest          CCMP self-test",
        "",
        "ABOUT    [EXP] ACTIVE isolation audit. The",
        "         offensive side of netspy - it",
        "         TRANSMITS at a chosen victim from",
        "         the ns list. Confirms before fire.",
        "",
        "ATTACKS  auto  probe+recommend (start here)",
        "         inject GTK ARP (proven)  bounce ARP",
        "         portdown capture->SD  mitm poison+cap",
        "         portup gw-poison  dns RA-poison",
        "",
        "HOW      Software-CCMP-encrypts a broadcast",
        "         frame with the live GTK + spoofs the",
        "         AP MAC (80211_tx) to reach clients",
        "         past isolation. inject is HW-proven.",
        "",
        "KEYS     [k] keyid 1/2   [q] stop",
        "NOTE     Real traffic MITM is NOT achievable",
        "         (single radio); mitm/portup/dns are",
        "         [exp] + report honestly whether a",
        "         poison holds (it usually won't).",
        "         dns is IPv6-only: needs real client",
        "         IPv6 (dead on v4-only nets/hotspots).",
        "         Best use: audit isolation + recon +",
        "         inject. Connect (cw). Own nets only.",
        nullptr
    }},

    { "arpspoof", "as", {
        "SYNTAX   as <victim> [gateway]",
        "         victim = ip | nd# | ns#",
        "",
        "ABOUT    [EXP] L2 ARP cache poisoning. Tells",
        "         the victim 'gateway is at me' and the",
        "         gateway 'victim is at me', so their",
        "         caches point to us. Needs cw first.",
        "",
        "HOW      Injects ARP replies as a raw ethernet",
        "         frame via the STA netif (driver adds",
        "         WPA2 encryption). Resolves + heals both",
        "         caches with the real MACs on [q] exit.",
        "",
        "LOG      Sniffs the redirected uplink + shows/",
        "         logs what the victim reaches (dst IP,",
        "         DNS domain, HTTP host) live + to",
        "         /apps/arpspoof/NNN.csv.",
        "",
        "KEYS     [q] stop + heal",
        "NOTE     Single radio = NO forwarding, so this",
        "         is a redirect/blackhole (DoS): you SEE",
        "         the victim's requests but can't relay",
        "         them. Own nets only.",
        nullptr
    }},

    { "responder", "rsp", {
        "SYNTAX   rsp",
        "",
        "ABOUT    [EXP] Name-service poisoner + NTLM",
        "         capture. Answers LLMNR/NBT-NS/mDNS",
        "         queries (incl. wpad) with our IP,",
        "         then fake HTTP(:80) + SMB(:445) auth",
        "         grabs the victim's hash. Needs cw.",
        "",
        "CATCH    NetNTLMv2 (-m 5600) + NTLMv1 (-m 5500)",
        "         over HTTP & SMB; HTTP Basic (cleartext);",
        "         serves a WPAD PAC to entice proxy auth.",
        "",
        "OUTPUT   /apps/responder/hashes.txt (hashcat)",
        "         log.csv (captures) + poison.csv (every",
        "         poisoned query: proto,src,name).",
        "",
        "KEYS     [q] stop",
        "NOTE     Captures for OFFLINE cracking (not",
        "         cracked on-device). SMB2 path is",
        "         best-effort/[EXP]. Own networks only.",
        nullptr
    }},

    { "portscan", "ps", {
        "SYNTAX   ps <ip|#|ns#> <start> <end>",
        "         ps top <ip|#|ns#>",
        "",
        "ABOUT    TCP scan — 4 parallel tasks,",
        "         150ms timeout. b = banner grab.",
        "         'top' scans 26 common ports.",
        "         # = nd index, ns# = netspy index.",
        "",
        "EXAMPLE  ps 192.168.1.1 1 1024",
        "         ps 3 80 443   (nd index #3)",
        "         ps top ns2    (netspy dev #2)",
        "KEYS     [b] banner  [l]/[a] pages  [q]",
        nullptr
    }},

    { "ping", "pg", {
        "SYNTAX   pg <ip|hostname|#|ns#>",
        "",
        "ABOUT    Continuous ICMP ping. Rolling",
        "         results + live sent/recv/loss and",
        "         min/avg/max RTT.",
        "         # = nd index, ns# = netspy index.",
        "",
        "EXAMPLE  pg 192.168.1.1   pg google.com",
        "         pg 0   pg ns3",
        "KEYS     q = stop",
        nullptr
    }},

    { "ssh", "sc", {
        "SYNTAX   ssh <ip|name> [user]",
        "         ssh save <name> <ip> [user] [port]",
        "         ssh list | ssh rm <name>",
        "",
        "ABOUT    Interactive SSH client (libssh).",
        "         Password auth + PTY shell with a",
        "         colour terminal + scrollback.",
        "         Connect WiFi first (cw).",
        "",
        "HOSTS    Saved profiles in /apps/ssh/hosts.csv",
        "         (name,ip,port,user — NO password).",
        "         ssh save nas 192.168.1.50 admin",
        "         ssh nas    -> connects, asks pass",
        "",
        "KEYS     type = send to remote shell",
        "         [trackpad up/down] scroll history",
        "         [click] disconnect",
        "",
        "NOTE     host-key check + key auth = planned.",
        nullptr
    }},

    { "buddy", "bd", {
        "SYNTAX   bd [name]",
        "",
        "ABOUT    Claude Desktop remote via BLE NUS.",
        "         Shows live stats + ASCII pet.",
        "         Approve/deny permission prompts",
        "         from the T-DECK keyboard.",
        "",
        "KEYS     [y] approve  [n] deny",
        "         [spc] pet  [q] quit",
        "NOTE     Claude Desktop > Developer >",
        "         Hardware Buddy. Stats in NVS.",
        nullptr
    }},

    { "game", "gm", {
        "SYNTAX   gm [<rom.nes>]",
        "",
        "ABOUT    NES emulator (Anemoia core, mappers 0-4+069).",
        "         ROMs: /apps/nes/roms/<name>.nes on SD.",
        "         No args: retro ROM picker.",
        "         With filename: load directly.",
        "",
        "KEYS     WASD / trackball = D-pad",
        "         [k]=B  [l]=A  [spc]=Select",
        "         Enter / trackball-click = Start",
        "         [e] save state  [r] load state",
        "         [q] quit emulator",
        "",
        "STATES   One slot per ROM, keyed by CRC32.",
        "         Saved to /apps/nes/states/<CRC32>.state",
        "         Toast confirms save/load for 1.5s.",
        "",
        "AUDIO    Vol controlled by [vol] command.",
        "         I2S (BCK=7/WS=5/DOUT=6).",
        "         Silenced by lock screen + undercover.",
        nullptr
    }},

    { "scanblue", "sbl", {
        "SYNTAX   scanblue",
        "",
        "ABOUT    BLE device scan — name, MAC, RSSI.",
        "         Paginated table.",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        "NOTE     Run before trackme for a baseline.",
        nullptr
    }},

    { "bleinfo", "bi", {
        "SYNTAX   bi <index|mac|all>",
        "",
        "ABOUT    BLE GATT client — enumerate services,",
        "         read characteristics, interact+audit.",
        "",
        "STEPS    1. sbl  2. bi <idx>  3. use keys below",
        "         bi all — connect+save every sbl result",
        "",
        "KEYS     [n] sniff — notify+indicate, saves SD",
        "         [w] write — hex/ASCII to char",
        "         [f] fuzz — seq/random/boundary/",
        "             oversized(MTU)/flood(DoS)",
        "         [g] abuse — read-hammer every char",
        "             (ignores R prop): LEAK=no-auth read",
        "         [b] audit — security posture:",
        "             link enc? JustWorks/MITM? bonded?",
        "             chars readable/writable no-auth",
        "             + value leak scan (keys/PINs)",
        "         [r] wcap — replay captured notif",
        "         [p] pair — bond + MITM + passkey",
        "         [s] save — GATT tree to /apps/bleinfo/",
        "",
        "NOTE     [f]/[g] result screens wait for [q];",
        "         they do not auto-close. [g] opens a",
        "         2nd link — 'Reconnect failed' if the",
        "         device allows only one connection.",
        "",
        "RISK     ! high   ~ med   orange low",
        "FILES    /apps/bleinfo/<mac>.txt",
        nullptr
    }},

    { "trackme", "tm", {
        "SYNTAX   tm [silent]",
        "",
        "ABOUT    Anti-tracking scanner. 60s baseline",
        "         learns your devices. Gate3 (200m GPS)",
        "         needed for WARNING/ALERT.",
        "DETECTS  AirTag, Tile, SmartTag, Chipolo,",
        "         Pebblebee, Google FindMy (svc-UUID).",
        "STEPS    1. gps on  2. tm (start before leaving)",
        "         3. Move 200m to confirm Gate3",
        "KEYS     [v] view  [o] sort  [f] filter  [h] help",
        "         [w] whitelist  [s] save  [c] clear  [q] quit",
        "FILES    /apps/trackme/known.csv  whitelist",
        "         /apps/trackme/signatures.csv  custom",
        nullptr
    }},

    { "bmon", "bm", {
        "SYNTAX   bmon",
        "",
        "ABOUT    Passive BLE advertisement sniffer.",
        "         Decodes iBeacon, Eddystone-UID/URL/TLM,",
        "         cleartext names, and unknown MFR data.",
        "         Passive only — does not send SCAN_REQ.",
        "",
        "TYPES    iBCN  Apple iBeacon (UUID+Major+Minor+TxPow)",
        "         E-UID Eddystone namespace+instance",
        "         E-URL Eddystone URL beacon",
        "         E-TLM Eddystone telemetry (batt/temp/uptime)",
        "         CLRT  cleartext device name",
        "         UNKN  unknown manufacturer data",
        "",
        "DISPLAY  TYPE  MAC               AT   RSSI  INFO",
        "         iBCN  AA:BB:CC:DD:EE:FF rnd  -055  UUID...",
        "         AT = addr type: pub=public rnd=random",
        "         Detail pane below list shows full extended data",
        "         for the selected row (trackpad up/down to select).",
        "",
        "LOG      [s] start/stop → /apps/bmon/NNN.csv",
        "         Cols: timestamp,first_seen,mac,addr_type,",
        "               type,rssi,sightings,info,extended",
        "         extended = full UUID / NS+Instance / TLM /",
        "           full MFR hex · dedup 60s per MAC",
        "         Timestamp from GPS/NTP if available.",
        "",
        "KEYS     [s]log  [a/l]page  trackpad↑↓ select  [q]quit",
        nullptr
    }},

    { "fastpair", "fp", {
        "SYNTAX   fp [scan|spam|h <idx>|h all]",
        "",
        "ABOUT    Google Fast Pair attack suite.",
        "         scan — BLE scan for FP devices.",
        "         spam — flood Android pairing popups.",
        "         h <idx> — GATT hijack CVE-2025-36911.",
        "         h all  — test all scanned devices.",
        "",
        "KEYS     [h+#] hijack  [s] spam  [q] quit",
        "FILES    /apps/fastpair/keys.csv",
        "         /apps/fastpair/sniff.csv",
        "         /apps/fastpair/paired.csv",
        "NOTE     Device needs pairing mode for h <idx>.",
        nullptr
    }},

    { "blespam", "bs", {
        "SYNTAX   bs [apple|android|ms|samsung|all]",
        "",
        "ABOUT    BLE notification spam suite.",
        "         apple   - iOS popups (AirPods/Beats)",
        "         android - Google Fast Pair popups",
        "         ms      - Windows Swift Pair popup",
        "         samsung - Galaxy manufacturer flood",
        "         all     - cycle all four vendors",
        "",
        "KEYS     [l/a] next/prev type  [q] stop",
        "NOTE     MAC randomized per advertisement.",
        nullptr
    }},

    { "usbmsc", "um", {
        "SYNTAX   usbmsc",
        "",
        "ABOUT    Expose SD card as USB mass storage.",
        "         SD card mounts on the connected PC.",
        "         All SD access on T-Rex is blocked",
        "         while drive is active.",
        "",
        "KEYS     [q] eject and return to normal mode",
        "NOTE     Eject on PC before pressing q.",
        nullptr
    }},

    { "usbkbd", "uk", {
        "SYNTAX   usbkbd",
        "",
        "ABOUT    T-Deck as USB keyboard + mouse.",
        "         Keyboard types into host. Trackball",
        "         moves cursor (accelerated).",
        "",
        "CLICK    tap=left  hold=right  1.5s=exit",
        "NOTE     BS auto-repeats after 500ms hold.",
        nullptr
    }},

    { "btkbd", "bk", {
        "SYNTAX   btkbd",
        "",
        "ABOUT    T-Deck as BLE keyboard + mouse.",
        "         Pair with any Bluetooth host.",
        "         Keyboard types into host. Trackball",
        "         moves cursor (accelerated).",
        "",
        "PAIR     Just Works — no passkey needed.",
        "         Advertises as: T-REX-KBD",
        "",
        "CLICK    tap=left  hold=right  1.5s=exit",
        "NOTE     BS auto-repeats after 1s hold.",
        "         Reconnects automatically on drop.",
        nullptr
    }},

    { "jiggle", "jg", {
        "SYNTAX   jiggle [ble]",
        "",
        "ABOUT    Mouse jiggler. Nudges cursor +2/-2px",
        "         every 30s to prevent screen lock.",
        "         Cursor returns to original position.",
        "",
        "MODE     jg       USB HID (plug in cable)",
        "         jg ble   BLE HID (pair on host)",
        "",
        "USE      USB: plug into target PC, run jg.",
        "         BLE: run jg ble, pair T-REX-KBD on",
        "         the host, leave unattended.",
        "",
        "EXIT     q",
        nullptr
    }},

    { "usbexec", "ux", {
        "SYNTAX   ux                  interactive menu",
        "         ux demo             built-in demo (USB)",
        "         ux <path>           run SD script (USB)",
        "         ux auto [dir]       probe OS, auto-pick",
        "         ux remote [ssid]    SoftAP web trigger",
        "         ux ble              BLE interactive menu",
        "         ux ble <demo|path>  BLE HID script run",
        "         ux ble clone <m|#>  BLESA spoof [EXP]",
        "         ux ble name \"X\"     fake BLE name",
        "",
        "ABOUT    BadUSB / DuckyScript executor.",
        "         Scripts: /apps/badusb/scripts/ on SD.",
        "         OS scripts: /apps/badusb/os/<os>/",
        "",
        "AUTO     Toggles NumLock, reads LED feedback.",
        "         Windows default=NumLock ON (toggles OFF)",
        "         Linux default=NumLock OFF (toggles ON)",
        "         macOS: no LED response -> unknown.",
        "         Picks scripts from os/<os>/ subdir.",
        "         Falls back to full picker if unknown.",
        "",
        "REMOTE   Starts SoftAP (default SSID=T-REX-CMD)",
        "         while USB cable is in victim PC.",
        "         Phone opens 192.168.4.1 in browser,",
        "         picks a script, fires it. 3s countdown",
        "         on T-Deck before execution (q=cancel).",
        "",
        "BLE      T-DECK advertises as keyboard (no bond).",
        "         clone: spoof a bonded kbd so a BLESA-",
        "         vulnerable host auto-reconnects [EXP].",
        "         name: override advertised name (both).",
        "         Aborts mid-script if host disconnects.",
        "",
        "CMDS     REM // DELAY DEFAULT_DELAY",
        "         STRING STRINGLN REPEAT F1-F24",
        "         CTRL-ALT GUI-SHIFT (hyphen combos)",
        "         HOLD <k> / RELEASE  WAIT_FOR_BUTTON_PRESS",
        nullptr
    }},

    { "sdinfo", "sdi", {
        "SYNTAX   sdinfo",
        "",
        "ABOUT    Show SD card type and capacity.",
        nullptr
    }},

    { "sdls", "ls", {
        "SYNTAX   ls [path]",
        "",
        "ABOUT    List SD directory (non-recursive).",
        "         No arg = current directory (cwd).",
        "         Dirs shown in cyan with trailing /.",
        "         Relative paths resolve from cwd.",
        "",
        "EXAMPLE  ls",
        "         ls /logs",
        "         ls badusb",
        nullptr
    }},

    { "cd", "cd", {
        "SYNTAX   cd <dir|..>",
        "         cd /",
        "",
        "ABOUT    Change the current working directory.",
        "         Affects ls, rm, ux path lookup.",
        "         Relative and absolute paths supported.",
        "         cd with no arg or / returns to root.",
        "",
        "EXAMPLE  cd /apps/badusb",
        "         cd /apps",
        "         cd ..",
        nullptr
    }},

    { "cat", "cat", {
        "SYNTAX   cat <path>",
        "",
        "ABOUT    Read and display file from SD.",
        "         Scrollable viewer — up to 400 lines.",
        "         Paths resolve from current directory.",
        "",
        "EXAMPLE  cat /apps/wpasniff/cracked.csv",
        "         cat /config/pwrsave.conf",
        "KEYS     tpad UP/DN scroll  [q] quit",
        nullptr
    }},

    { "edit", "ed", {
        "SYNTAX   edit <path>",
        "",
        "ABOUT    nano-style text editor for SD.",
        "         Missing file -> new (made on save).",
        "         Up to 500 lines; bigger = read-only.",
        "",
        "KEYS     type to insert  Bksp delete",
        "         Enter splits line (auto-indent)",
        "         tpad U/D/L/R = move cursor",
        "         (roll fast = jump/page)",
        "         click = MENU",
        "MENU     Save / Save As / Find / Go to",
        "         line / Top / Bottom / Undo /",
        "         Cut line / Paste line / Exit",
        "         Undo = 1 step. Exit prompts",
        "         if unsaved.",
        "",
        "NOTE     Paths relative to cwd (cd) —",
        "         no leading / needed.",
        "EXAMPLE  ed notes.txt",
        "         ed wpasniff/wordlist.txt",
        nullptr
    }},

    { "rm", "rm", {
        "SYNTAX   rm <path>",
        "         rm -d <dir>",
        "",
        "ABOUT    Delete a file from SD card.",
        "         rm -d removes a directory and",
        "         ALL its contents (recursive).",
        "WARNING  Files: no confirmation.",
        "         rm -d: asks y/N first.",
        "         Won't delete root or the cwd.",
        "NOTE     Paths are relative to cwd —",
        "         no leading / needed.",
        "",
        "EXAMPLE  rm creds.csv",
        "         rm -d oldtool",
        nullptr
    }},

    { "sdformat", "sdf", {
        "SYNTAX   sdf [init]",
        "",
        "ABOUT    Format SD card to FAT32.",
        "WARNING  Destroys all data. Press y.",
        "MODES    sdf init - Format + init",
        nullptr
    }},

    { "gps", "gps", {
        "SYNTAX   gps on|off|test",
        "",
        "ABOUT    on   — start GPS background task.",
        "                Shows live fix status.",
        "                Task keeps running on quit.",
        "         off  — stop GPS background task.",
        "         test — one-shot coordinate read.",
        "                Lat, lon, alt, speed, sats.",
        "",
        "NOTE     Cold fix ~4 min outdoors.",
        "         Run gps on before trackme.",
        "         T-Deck Plus only.",
        nullptr
    }},

    { "notes", "nt", {
        "SYNTAX   (opened from the `home` launcher — no command)",
        "",
        "ABOUT    [EXP] Notes app — opened from the `home`",
        "         launcher's Notes tile (no standalone command).",
        "         Real, working notes: SD-backed files, keyboard",
        "         typing, and a cursor-addressable editor.",
        "         Renders an ordinary",
        "         notes app (warm paper, amber accent, no",
        "         terminal cues).",
        "",
        "USE      Tap a card (or trackball UP/DOWN + click) to",
        "         open a note; tap the + FAB for a new note.",
        "         Inside a note: tap any line to move the",
        "         cursor there and type/backspace/Enter to edit;",
        "         trackball UP/DOWN moves line-to-line, LEFT/",
        "         RIGHT moves char-by-char (wraps across lines).",
        "         Tap the save icon (or [q]) to save. Tap the",
        "         back chevron to return to the list.",
        "",
        "NOTE     Notes save to /apps/notes/NNN.txt on SD.",
        "         No SD → notes work for the session only",
        "         (nothing persists, no crash). Duress/decoy and",
        "         boot-cover are still TODO — see `man uc`.",
        nullptr
    }},

    { "undercover", "uc", {
        "SYNTAX   undercover          — enter cover",
        "         uc set / uc clear   — set/remove passphrase",
        "         uc status           — passphrase+boot+panic",
        "         uc boot on|off      — boot directly into Notes",
        "         uc panic set|off    — set/disable instant-hide",
        "",
        "ABOUT    [EXP] Drop into undercover mode — the same",
        "         phone HOME-SCREEN disguise as `home`, but",
        "         raises the covert flag so the device goes",
        "         SILENT: notif sounds + hidden-SSID beep",
        "         suppressed, real status bar hidden. Passive",
        "         tools keep logging underneath — only the tells",
        "         go quiet. The Notes app is on the home grid.",
        "",
        "USE      Navigate like `home` (see `man home`). Type",
        "         the passphrase anywhere in the cover to exit",
        "         silently to the CLI. [q] exits only when no",
        "         passphrase is set.",
        "",
        "PANIC    `uc panic set` — press a key to arm it; then",
        "         pressing it ANYWHERE (even mid-command) drops",
        "         into the cover. Default '@', fires only once a",
        "         passphrase is set. Reserved keys (' q space",
        "         Enter Bksp) blocked. `uc panic off` disables.",
        "",
        "BOOT     `uc boot on` boots straight into the home",
        "         cover (no splash). A lock PIN then fires AFTER",
        "         the passphrase exits it. `uc boot off` = normal.",
        "",
        "NOTE     Passphrase = SHA-256(salt+phrase) in",
        "         /config/undercover.conf — never plaintext.",
        nullptr
    }},

    { "home", "hm", {
        "SYNTAX   home",
        "",
        "ABOUT    [EXP] 'Home launcher' cover UI — a modern",
        "         phone-style home screen disguise, alternative",
        "         to the Notes-only cover. Fake status bar +",
        "         live clock/weather hero + a 4x2 app grid",
        "         (Phone/Messages/Email/Browser/Music/Notes/",
        "         Calendar/Settings).",
        "",
        "USE      Tap a tile (or trackball to highlight + click).",
        "         ONLY the Notes tile opens anything — it launches",
        "         the real notes app (see `man notes`); every",
        "         other tile just shows a 'No service' toast.",
        "         In the opened notes app, tap the back chevron",
        "         in the notes LIST (or press [q]) to return HERE.",
        "         Inside a note, the detail back chevron goes to",
        "         the list first.",
        "         Drag/trackball navigates. Type the undercover",
        "         passphrase anywhere to drop to the CLI; [q]",
        "         at the home screen exits only when no passphrase.",
        "",
        "LIVE     Status-bar clock + battery and the hero",
        "         weather are REAL: clock via ClockManager,",
        "         battery via BatteryManager, weather via `wx`",
        "         (fetched on entry when WiFi is up).",
        "",
        "NOTE     This is the disguise `undercover` uses (and the",
        "         boot cover). `home` runs it standalone (no",
        "         silent mode) for tuning. See `man uc`.",
        nullptr
    }},

    { "weather", "wx", {
        "SYNTAX   wx                     — status",
        "         wx loc <lat> <lon>     — set location",
        "         wx units metric|imperial",
        "         wx now                 — fetch now + show",
        "",
        "ABOUT    Current weather for the `home` launcher hero,",
        "         from the Open-Meteo API — FREE and KEYLESS (no",
        "         API key, nothing secret on the SD card).",
        "         Weather is LIVE server data — it CANNOT be",
        "         known offline; GPS only supplies the location.",
        "         Needs WiFi (`cw`). HTTPS (setInsecure).",
        "",
        "LOC      Auto, best source: GPS fix (Plus, most",
        "         accurate) > manual `wx loc` > WiFi IP",
        "         geolocation (coarse, ip-api.com). So it just",
        "         works — `wx loc` is only an optional override.",
        "         Last reading is cached (shown offline til reboot).",
        "",
        "NOTE     Config /config/weather.conf (lat/lon/units) —",
        "         SELF-SEEDED as a commented template on first",
        "         boot. On the Plus it just works with a GPS fix.",
        nullptr
    }},

    { "test", "tst", {
        "SYNTAX   test spk | test mic | test lora | test touch",
        "",
        "ABOUT    Hardware self-tests. Pick a target:",
        "",
        "SPK      I2S speaker test — raw tones at full",
        "         volume + notif level test (nf settings).",
        "         [1]-[6] tones  [s] C scale  [a]lert",
        "         [w]arning [c]success [i]nfo [p]ing [q].",
        "         Notif keys honour nf vol + MP3.",
        "MIC      ES7210 mic test — live level meter,",
        "         voice-activity detect, record 3s+replay.",
        "         [r]ecord [p]lay [+/-]gain [q]. Both boards.",
        "LORA     SX1262 diagnostic — init, TX test, RX",
        "         monitor, 868/915 MHz. [q] stop RX.",
        "TOUCH    [EXP] GT911 capacitive touch — live",
        "         crosshair tracks the finger, shows raw",
        "         mapped x/y + tap/long-press/drag events.",
        "         Corner brackets verify no mirror/swap.",
        nullptr
    }},

    { "i2cscan", "isc", {
        "SYNTAX   isc",
        "         isc r <addr> <reg> [len]",
        "         isc raw <addr> [len]",
        "         isc w <addr> <reg> <val>",
        "         isc d <addr>",
        "",
        "ABOUT    I2C bus scanner (SDA:18 SCL:8).",
        "         Probes 0x08-0x77. Rows color-coded by",
        "         device type. Detail pane auto-reads.",
        "         Raw fallback for stream/16-bit devices.",
        "",
        "CMDS     isc           scan + interactive UI",
        "         isc r 40 00 8 read 8B via reg ptr",
        "         isc raw 5d 4  raw stream read (GT911)",
        "         isc w 40 07 0 write reg 0x07 = 0x00",
        "         isc d 55      256-byte hex dump",
        "",
        "KEYS     pad↑↓  select (auto-flip page)",
        "         CLICK/[r]  register browser",
        "         [d]  256-byte hex dump",
        "         [p]  re-probe selected device",
        "         [v]  verify ALL devices (live ACK)",
        "         [w]  write reg (in reg browser)",
        "         [s]  save to SD",
        "         [f]  rescan bus  [q] quit",
        "FILES    /apps/i2cscan/results.csv",
        "",
        "NOTE     [EXPERIMENTAL] Not field-tested.",
        nullptr
    }},

    { "csidetect", "csi", {
        "SYNTAX   csi          (use connected link)",
        "         csi auto     (any AP, no connect)",
        "",
        "ABOUT    WiFi CSI motion detector (sweep-style",
        "         display, NOT an actual radar). A moving",
        "         body disturbs WiFi signal echoes; this",
        "         senses that disturbance.",
        "         csi      : reads the connected AP (cw)",
        "         csi auto : scans, locks the strongest",
        "                    AP's beacons - NO router join",
        "",
        "READ     CLEAR (green)  = no motion",
        "         CONTACT (red)  = movement near you",
        "         MOTION%        = how much",
        "         blips/sweep    = DECORATIVE; dial",
        "                          position is NOT a",
        "                          direction. Only centre",
        "                          + MOTION% are real.",
        "",
        "KEYS     a/l sens  c=recal  h=help  q=quit",
        "         t  adaptive threshold on/off",
        "         n  NBVI subcarrier weighting",
        "         s  log -> /apps/csidetect/NNN.csv",
        "",
        "LIMITS   Single antenna = motion energy only,",
        "         no direction/count/position. A still",
        "         person may read CLEAR. csi auto is",
        "         noisier than csi (beacon-rate).",
        "",
        "NOTE     [EXPERIMENTAL] environment-dependent;",
        "         best as a covert motion/occupancy hint.",
        nullptr
    }},

    { "espchat", "ec", {
        "SYNTAX   ec [pub|prv|bg|stop] [ch]",
        "",
        "ABOUT    ESP-NOW off-grid chat. No router.",
        "         200m+ LOS. Any ESP32/8266 can join",
        "         the public channel.",
        "",
        "MODES    ec          mode picker",
        "         ec pub [ch] broadcast (open)",
        "         ec prv <M>  private 1:1 AES-128",
        "         ec bg [ch]  background listener",
        "         ec stop     stop background",
        "         ec pub set <ch>  save default ch",
        "",
        "PAIR     Initiator: [click] in pub to pair.",
        "         PIN shown → receiver types it.",
        "         3 attempts — fail = remove contact.",
        "         Contact saved to SD or RAM (no SD).",
        "         RAM contacts cleared on reboot.",
        "",
        "NOTIF    Public msg  = PING  (short beep)",
        "         Private msg = INFO  (longer beep)",
        "         Pair req    = WARN  (double beep)",
        "",
        "FILES    /apps/espchat/pub/chN.log",
        "         /apps/espchat/prv/<MAC>.log",
        "         /apps/espchat/contacts.csv",
        "KEYS     [+/-]ch  [trk]scroll  [hold trk]exit",
        nullptr
    }},

    { "espsniff", "es", {
        "SYNTAX   es [ch]",
        "",
        "ABOUT    Passive ESP-NOW frame sniffer.",
        "         Captures all ESP-NOW action frames.",
        "",
        "KEYS     [c]hop/lock [+/-]ch [j/k]select",
        "         [Enter]detail [a/l]page [s]save",
        "         [f]filter [x]clear [q]quit",
        "FILES    /apps/espsniff/NNN.csv + NNN.pcap",
        nullptr
    }},

    { "esptest", "est", {
        "SYNTAX   est [ch]",
        "",
        "ABOUT    ESP-NOW TX+RX test tool.",
        "         Broadcasts every 2s, shows RX log.",
        "",
        "KEYS     [+/-]ch  [q]quit",
        nullptr
    }},

    { "espvoice", "ev", {
        "SYNTAX   ev [ch]",
        "",
        "ABOUT    ESP-NOW walkie-talkie. G.722 HD",
        "         voice (16kHz wideband). Broadcast —",
        "         any T-Deck on same ch hears you.",
        "         Mic on both T-Deck & Plus.",
        "",
        "TALK     [space] = push-to-talk TOGGLE",
        "         (keyboard has no key-up, so press",
        "         to talk, press again to listen).",
        "         Roger beep + RECEIVING tag mark",
        "         start/end like a real radio.",
        "",
        "AUDIO    [+/-] RX volume (local, 0-150%)",
        "         [o/p] TX mic gain (clean louder)",
        "         Neither touches global vol.",
        "",
        "KEYS     [space]talk [+/-]vol [o/p]gain",
        "         [,/.]ch  [q]quit",
        nullptr
    }},
};

static const int PAGE_COUNT = (int)(sizeof(PAGES) / sizeof(PAGES[0]));

// ── Label keywords colored grey in output ─────────────────────────────────────

static const char* LABELS[] = {
    "SYNTAX", "ABOUT", "STEPS", "EXAMPLE", "KEYS",
    "NOTE", "FILES", "OPTIONS", "WARNING", "DETECTS",
    "RECOVER", "LOCK", "TIMEOUT", "UNLOCK", "RISK",
    "CMDS", "MODES", "USE", "EXIT", "CLICK", "LEVELS", nullptr
};

// ── Class implementation ──────────────────────────────────────────────────────

ManPages::ManPages(DisplayManager& dm) : _dm(dm) {}

void ManPages::renderPage(int idx, int scrollTop, int total) {
    const ManEntry& pg = PAGES[idx];

    _dm.clearScreen();
    _dm.setCursor(10, outputY);

    // Header: [MAN::CMDNAME]  idx/total
    char upname[20]; int ci = 0;
    for (const char* p = pg.cmd; *p && ci < 19; p++, ci++)
        upname[ci] = toupper((unsigned char)*p);
    upname[ci] = '\0';
    char pgbuf[8]; snprintf(pgbuf, sizeof(pgbuf), "%d/%d", idx + 1, PAGE_COUNT);

    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("MAN");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.printText(upname);
    _dm.setTextColor(0x7BEF);    _dm.printText("]  ");
    _dm.setTextColor(0x7BEF);    _dm.println(pgbuf);
    _dm.printSeparator();

    int contentTop = _dm.getCursorY();
    int y = contentTop;

    // Content — render only the visible window
    for (int i = scrollTop; i < scrollTop + MAN_VISIBLE && i < total; i++) {
        const char* line = pg.lines[i];
        _dm.setCursor(10, y);
        if (line[0] != '\0') {
            bool labeled = false;
            for (int k = 0; LABELS[k]; k++) {
                int llen = strlen(LABELS[k]);
                if (strncmp(line, LABELS[k], llen) == 0 && line[llen] == ' ') {
                    _dm.setTextColor(0x7BEF);
                    _dm.printText(LABELS[k]);
                    _dm.setTextColor(TFT_WHITE);
                    _dm.printText(line + llen);
                    labeled = true;
                    break;
                }
            }
            if (!labeled) {
                _dm.setTextColor(TFT_WHITE);
                _dm.printText(line);
            }
        }
        y += LINE_HEIGHT;
    }

    // Scrollbar — only when content overflows
    if (total > MAN_VISIBLE) {
        int barX    = SCREEN_WIDTH - 5;
        int barH    = MAN_VISIBLE * LINE_HEIGHT;
        int thumbH  = max(4, barH * MAN_VISIBLE / total);
        int maxTop  = total - MAN_VISIBLE;
        int thumbY  = contentTop + (maxTop > 0
                        ? (barH - thumbH) * scrollTop / maxTop
                        : 0);
        _dm.fillRect(barX, contentTop, 3, barH, 0x2104);   // track (dark)
        _dm.fillRect(barX, thumbY,     3, thumbH, TFT_CYAN); // thumb
    }

    // Nav bar — fixed position below content area
    int navY = contentTop + MAN_VISIBLE * LINE_HEIGHT + 2;
    _dm.fillRect(5, navY, 310, 1, TFT_CYAN);
    _dm.setCursor(10, navY + 3);
    _dm.setTextColor(0x7BEF);    _dm.printText("tpad ");
    _dm.setTextColor(TFT_GREEN); _dm.printText("UP/DN");
    _dm.setTextColor(0x7BEF);    _dm.printText(" scroll  [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("a");
    _dm.setTextColor(0x7BEF);    _dm.printText("]prev [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("l");
    _dm.setTextColor(0x7BEF);    _dm.printText("]next [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("q");
    _dm.setTextColor(0x7BEF);    _dm.printText("]quit");
    _dm.setTextColor(TFT_WHITE);
}

// Count non-null lines in a ManEntry
static int countLines(const ManEntry& pg) {
    int n = 0;
    while (n < 31 && pg.lines[n]) n++;
    return n;
}

void ManPages::show(char* args) {
    if (!args || !*args) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Usage: man <command>");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Example: man wpasniff");
        _dm.printCommandScreen();
        return;
    }

    int found = -1;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcmp(args, PAGES[i].cmd) == 0 || strcmp(args, PAGES[i].shortName) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        _dm.setCursor(10, _dm.getCursorY());
        char buf[52]; snprintf(buf, sizeof(buf), "No manual entry for '%s'.", args);
        _dm.println(buf);
        _dm.printCommandScreen();
        return;
    }

    int scrollTop = 0;
    int total     = countLines(PAGES[found]);
    renderPage(found, scrollTop, total);

    while (true) {
        char         key = inputHandler.getKeyboardInput();
        TrackballEvent evt = inputHandler.getTrackballEvent();

        if (key == 'q' || key == 'Q') { _dm.clearInputText(); return; }

        bool redraw = LockScreenManager::getInstance().consumeJustUnlocked();

        // Scroll within page
        if ((evt == TBALL_UP)   && scrollTop > 0) {
            scrollTop--; redraw = true;
        }
        if ((evt == TBALL_DOWN) && scrollTop < total - MAN_VISIBLE) {
            scrollTop++; redraw = true;
        }

        // Navigate between man pages (keyboard only — trackball left/right excluded, too sensitive)
        if ((key == 'l' || key == 'L') && found < PAGE_COUNT - 1) {
            found++; scrollTop = 0; total = countLines(PAGES[found]); redraw = true;
        }
        if ((key == 'a' || key == 'A') && found > 0) {
            found--; scrollTop = 0; total = countLines(PAGES[found]); redraw = true;
        }

        if (redraw) renderPage(found, scrollTop, total);
    }
}
