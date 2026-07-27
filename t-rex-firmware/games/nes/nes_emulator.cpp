// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// games/nes — NES emulator via the vendored Anemoia-ESP32 core (GPL-3.0), in-tree
//   at games/nes/anemoia/. Upstream: https://github.com/Shim06/Anemoia-ESP32
//   Credited in NOTICES #20; this file is T-REX's integration layer, not the core.
//
// Display: 256×240 NES output centred on the 320×240 landscape screen
//   (32px black bar each side). LovyanGFX pushImage replaces TFT_eSPI.
// Audio:   APU writes directly to I2S_NUM_0. We install the driver with T-Deck
//   pins (BCK=7/WS=5/DOUT=6) before starting and uninstall on exit.
// Input:   WASD + trackball = D-pad · k=B · l=A · Enter=Start · Space=Select.
//          q = back to the ROM library (NOT the CLI); [q] in the library exits to CLI.
//          [e]=save state  [r]=load state  (one slot/ROM, /apps/nes/states/<CRC32>.state)
//   Decay-counter hold simulation keeps bits set for NES_HOLD_FRAMES frames.
// ROMs:    /apps/nes/roms/<name>.nes — trackball/WASD picker, Enter to load. After a
//          game exits, control returns to this picker so ROMs can be chained.
//
// Stack-safety: Bus (~6 KB) is heap-allocated (ps_malloc on ESP32-S3 with PSRAM).
//   The main FreeRTOS task has only 8 KB stack; Bus on the stack crashes immediately.

#include "nes_emulator.h"

#include <Arduino.h>
#include <SD.h>
#include <algorithm>
#include <vector>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Anemoia core (vendored in t-rex-firmware/games/nes/anemoia/)
#include "core/bus.h"
#include "core/cartridge.h"
#include "core/rom_backends.h"

// T-REX platform
#include "covert.h"
#include "utilities.h"        // BOARD_BOOT_PIN
#include "vol_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "powersave_manager.h"
#include "sdcard_manager.h"

extern LGFX             tft;
extern DisplayManager   displayManager;
extern InputHandling    inputHandler;

#define NES_W           256
#define NES_X_OFF       32      // (320 - 256) / 2
#define NES_HOLD_FRAMES 4
#define NES_APU_STACK   4096

static volatile bool s_nesQuit   = false;
static TaskHandle_t  s_apuTask   = nullptr;
static char          s_toast[32] = {};
static uint32_t      s_toastEnd  = 0;

static void showToast(const char* msg)
{
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = '\0';
    s_toastEnd = millis() + 1500;
}

static void drawToast()
{
    if (millis() >= s_toastEnd) return;
    tft.fillRect(0, 0, 320, 16, 0x2104);
    tft.setTextColor(TFT_WHITE, 0x2104);
    tft.setFont(nullptr);   // Font0 6×8
    tft.setTextSize(1);
    int tw = (int)strlen(s_toast) * 6;
    tft.setCursor((320 - tw) / 2, 4);
    tft.print(s_toast);
}

// ── display callback ──────────────────────────────────────────────────────────
static void IRAM_ATTR nesFlush(uint16_t* buf, uint16_t startLine, uint8_t count)
{
    if (displayManager.isBlocked()) return;
    tft.pushImage(NES_X_OFF, (int32_t)startLine, NES_W, (int32_t)count, buf);
}

// ── I2S setup ─────────────────────────────────────────────────────────────────
static bool nesI2sInit()
{
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = 44100,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = 0,
        .dma_buf_count        = 4,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return false;

    i2s_pin_config_t pins = {
        // mck MUST be explicit: an omitted designated-initializer field is
        // zero-initialized to 0 == GPIO0 == BOARD_BOOT_PIN (trackball click),
        // so leaving it out makes i2s_set_pin() route MCLK onto the click pin.
        // That leaves GPIO0 reading LOW after the game and the lockscreen's
        // 3-s trackpad-hold detector then fires on a loop. Pin it to NO_CHANGE.
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = I2S_BCLK_PIN,
        .ws_io_num    = I2S_LRC_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}

// ── APU task (core 0) ─────────────────────────────────────────────────────────
static void apuTask(void* param)
{
    Apu2A03* apu = static_cast<Apu2A03*>(param);
    while (true) {
        // Silence audio whenever the screen is covered — lock screen or undercover.
        // tx_desc_auto_clear=true means the DMA buffer fills with zeros on starvation.
        if (g_covert || LockScreenManager::getInstance().isLocked()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        apu->clock();
    }
}

// ── ROM picker — retro full-screen UI ─────────────────────────────────────────
//
// Layout (320×240):
//   y=  0..31  Header: dark-maroon bar, yellow "NES LIBRARY" title (Font0×2)
//   y= 32..207  List:  11 rows × 16px, orange highlight + red left bar for sel
//   y=208..239  Footer: controls hint, 2 lines
//
// Draws directly to tft — no displayManager (status bar won't fire during this
// blocking call since main.ino loop() is not running).
// ─────────────────────────────────────────────────────────────────────────────
static String pickRom()
{
    // ── collect bare filenames ────────────────────────────────────────────────
    std::vector<String> roms;
    File dir = SD.open(SD_DIR_NES);
    if (dir && dir.isDirectory()) {
        File f;
        while ((f = dir.openNextFile())) {
            String fullPath = f.name();   // ESP32 SD returns full path
            f.close();
            int slash = fullPath.lastIndexOf('/');
            String name = (slash >= 0) ? fullPath.substring(slash + 1) : fullPath;
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".nes")) roms.push_back(name);
        }
        dir.close();
    }
    std::sort(roms.begin(), roms.end());
    const int total = (int)roms.size();

    // ── layout ───────────────────────────────────────────────────────────────
    // y=0..29 is the status bar — leave it alone; picker lives below.
    static const int STATUS_H = 30;
    static const int HDR_H  = 26;                        // picker header
    static const int FTR_H  = 24;                        // picker footer
    static const int FTR_Y  = 240 - FTR_H;              // 216
    static const int LIST_Y = STATUS_H + HDR_H;          // 56
    static const int ROW_H  = 16;
    static const int ROWS   = (FTR_Y - LIST_Y) / ROW_H; // (216-56)/16 = 10

    // ── palette ───────────────────────────────────────────────────────────────
    static const uint16_t C_BG      = 0x0002;   // near-black navy
    static const uint16_t C_HDR_BG  = 0x6000;   // dark maroon
    static const uint16_t C_ACCENT  = 0xF800;   // bright red
    static const uint16_t C_TITLE   = 0xFFE0;   // NES yellow
    static const uint16_t C_SEL_BG  = 0xFC00;   // orange  (R=31 G=32 B=0)
    static const uint16_t C_SEL_TXT = 0x0000;   // black on orange
    static const uint16_t C_NUM     = 0x4208;   // dim grey row numbers
    static const uint16_t C_NAME    = 0xFFFF;   // white ROM names
    static const uint16_t C_ALT_BG  = 0x0004;   // barely-lighter alternate rows
    static const uint16_t C_FTR_TXT = 0x07FF;   // cyan footer
    static const uint16_t C_CNT     = 0x8410;   // medium grey counts

    // sel/scroll must be declared before any lambda that captures them by ref
    int sel = 0, scroll = 0;

    // ── strip .nes + truncate for display ─────────────────────────────────────
    auto dispName = [](const String& s) -> String {
        String n = s;
        if (n.length() >= 4) {
            String tail = n.substring(n.length() - 4);
            tail.toLowerCase();
            if (tail == ".nes") n = n.substring(0, n.length() - 4);
        }
        if ((int)n.length() > 43) { n = n.substring(0, 41); n += ".."; }
        return n;
    };

    // ── draw helpers ──────────────────────────────────────────────────────────
    auto drawHeader = [&]() {
        tft.fillRect(0, STATUS_H, 320, HDR_H, C_HDR_BG);
        tft.fillRect(0, STATUS_H, 320, 3, C_ACCENT);                    // top accent
        tft.fillRect(0, STATUS_H + HDR_H - 3, 320, 3, C_ACCENT);       // bottom accent
        // "NES LIBRARY" centred — Font0 size 2 = 12px/char × 16px tall
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextColor(C_TITLE, C_HDR_BG);
        const char* ttl = "NES LIBRARY";
        tft.setCursor((320 - (int)strlen(ttl) * 12) / 2, STATUS_H + 5);
        tft.print(ttl);
        // ROM count right-aligned, small text, vertically centered
        tft.setTextSize(1);
        tft.setTextColor(C_CNT, C_HDR_BG);
        char cnt[12]; snprintf(cnt, sizeof(cnt), "%d ROMs", total);
        tft.setCursor(320 - (int)strlen(cnt) * 6 - 4, STATUS_H + 9);
        tft.print(cnt);
    };

    auto drawFooter = [&]() {
        tft.fillRect(0, FTR_Y, 320, FTR_H, C_BG);
        tft.fillRect(0, FTR_Y, 320, 2, C_ACCENT);
        tft.setFont(nullptr); tft.setTextSize(1);
        tft.setTextColor(C_FTR_TXT, C_BG);
        tft.setCursor(4, FTR_Y + 4);
        tft.print("W/S  TBALL=nav  ENTER=load  Q=exit");
        tft.setTextColor(C_CNT, C_BG);
        tft.setCursor(4, FTR_Y + 14);
        tft.print("E=save state  R=load state");
    };

    // Draws a single visible list row (listRow=0..ROWS-1, romIdx=actual index)
    auto drawRow = [&](int listRow, int romIdx) {
        bool     isSel = (romIdx == sel);
        int      y     = LIST_Y + listRow * ROW_H;
        uint16_t bg    = isSel ? C_SEL_BG
                               : ((listRow & 1) ? C_ALT_BG : C_BG);

        tft.fillRect(0, y, 316, ROW_H, bg);   // leave 4px column for the scrollbar

        if (romIdx >= total) return;   // empty row below last item

        tft.setFont(nullptr); tft.setTextSize(1);

        // Red left accent on selected row
        if (isSel) tft.fillRect(0, y, 4, ROW_H, C_ACCENT);

        // Row number (right-aligned in 22px zone)
        char num[5]; snprintf(num, sizeof(num), "%3d", romIdx + 1);
        tft.setTextColor(isSel ? (uint16_t)0x6318 : C_NUM, bg);
        tft.setCursor(6, y + 4);
        tft.print(num);

        // Thin separator
        tft.setTextColor(isSel ? (uint16_t)0x8410 : (uint16_t)0x2104, bg);
        tft.setCursor(24, y + 4);
        tft.print("|");

        // ROM name
        tft.setTextColor(isSel ? C_SEL_TXT : C_NAME, bg);
        tft.setCursor(32, y + 4);
        tft.print(dispName(roms[romIdx]));
    };

    auto drawList = [&]() {
        for (int i = 0; i < ROWS; i++) drawRow(i, scroll + i);
    };

    // Scrollbar in the reserved 4px right column — thumb size/position reflect
    // how much of the library is visible. Drawn after the list each refresh.
    auto drawScrollbar = [&]() {
        const int SB_X   = 316;
        const int SB_W   = 4;
        const int trackH = ROWS * ROW_H;
        tft.fillRect(SB_X, LIST_Y, SB_W, trackH, C_ALT_BG);   // track
        if (total <= ROWS) {
            tft.fillRect(SB_X, LIST_Y, SB_W, trackH, C_CNT);  // all visible → full bar
            return;
        }
        int thumbH = (trackH * ROWS) / total;
        if (thumbH < 10) thumbH = 10;
        const int maxScroll = total - ROWS;
        const int thumbY = LIST_Y + (maxScroll > 0 ? (trackH - thumbH) * scroll / maxScroll : 0);
        tft.fillRect(SB_X, thumbY, SB_W, thumbH, C_ACCENT);   // thumb
    };

    // ── no-ROM screen ─────────────────────────────────────────────────────────
    if (total == 0) {
        tft.fillRect(0, STATUS_H, 320, 240 - STATUS_H, C_BG);
        drawHeader();
        tft.setFont(nullptr); tft.setTextSize(1);
        tft.setTextColor(C_ACCENT, C_BG);
        tft.setCursor(10, 80); tft.print("No ROMs found.");
        tft.setTextColor(C_CNT, C_BG);
        tft.setCursor(10, 96); tft.print("Copy .nes files to:");
        tft.setTextColor(C_FTR_TXT, C_BG);
        tft.setCursor(10, 112); tft.print(SD_DIR_NES);
        tft.setTextColor(C_CNT, C_BG);
        tft.setCursor(10, 148); tft.print("Press any key to exit.");
        while (inputHandler.getKeyboardInput() == 0) delay(20);
        return "";
    }

    // ── initial full render ───────────────────────────────────────────────────
    // Refresh the status bar (y=0..29): returning from a game leaves it black,
    // and main.ino's loop() — which normally repaints it — is not running here.
    displayManager.updateStatusBar();
    tft.fillRect(0, STATUS_H, 320, 240 - STATUS_H, C_BG);
    drawHeader();
    drawFooter();
    drawList();
    drawScrollbar();

    // ── input loop ────────────────────────────────────────────────────────────
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();

        if (k == 'q') return "";
        if (k == '\r' || k == '\n') return String(SD_DIR_NES) + "/" + roms[sel];

        bool moved = false;
        if (k == 'w' || tb == TBALL_UP) {
            if (sel > 0) { sel--; moved = true; }
        }
        if (k == 's' || tb == TBALL_DOWN) {
            if (sel < total - 1) { sel++; moved = true; }
        }
        if (moved) {
            if (sel < scroll)         scroll = sel;
            if (sel >= scroll + ROWS) scroll = sel - ROWS + 1;
            drawList();
            drawScrollbar();
        }
        delay(20);
    }
}

// ── controller hold simulation ────────────────────────────────────────────────
//   NES pad bit order: A=0 B=1 Sel=2 Start=3 Up=4 Down=5 Left=6 Right=7
static uint8_t s_ctrl = 0;
static uint8_t s_hold[8] = {};

static void ctrlPress(int bit, int frames = NES_HOLD_FRAMES)
{
    s_ctrl |= (1 << bit);
    s_hold[bit] = (uint8_t)frames;
}

static uint8_t ctrlUpdate(char k, TrackballEvent tb)
{
    for (int i = 0; i < 8; i++) {
        if (s_hold[i] > 0) s_hold[i]--;
        else                s_ctrl &= ~(1 << i);
    }
    if (tb == TBALL_UP    || k == 'w' || k == 'W') ctrlPress(4);
    if (tb == TBALL_DOWN  || k == 's' || k == 'S') ctrlPress(5);
    if (tb == TBALL_LEFT  || k == 'a' || k == 'A') ctrlPress(6);
    if (tb == TBALL_RIGHT || k == 'd' || k == 'D') ctrlPress(7);
    if (k == 'k' || k == 'K')              ctrlPress(1, 2);  // B
    if (k == 'l' || k == 'L')              ctrlPress(0, 2);  // A
    if (k == ' ')                           ctrlPress(2, 2);  // Select
    if (k == '\r' || k == '\n')             ctrlPress(3, 2);  // Start
    if (tb == TBALL_CLICK)                  ctrlPress(3, 2);  // trackball = Start
    return s_ctrl;
}

// ── run a single ROM ──────────────────────────────────────────────────────────
// Loads the cartridge, runs the emulation loop until the user presses [q], then
// tears everything down. Leaves the screen black on return — it does NOT restore
// the CLI, because runNesEmulator() loops back to the ROM library after each game.
// A load failure shows a 2 s error and returns (the caller drops back to the
// library, so the user isn't dumped to the CLI on a bad ROM).
static void runOneGame(const String& romPath)
{
    s_nesQuit = false;
    s_ctrl    = 0;
    memset(s_hold, 0, sizeof(s_hold));

    if (!SD.exists(romPath.c_str())) {
        displayManager.clearScreen();
        displayManager.setTextColor(TFT_RED);
        String msg = "ROM not found: " + romPath;
        displayManager.printText(msg);
        delay(2000);
        return;
    }

    // I2S audio
    if (!nesI2sInit()) {
        displayManager.clearScreen();
        displayManager.setTextColor(TFT_RED);
        displayManager.printText("I2S init failed");
        delay(2000);
        return;
    }

    // Cartridge
    Cartridge* cart = new Cartridge(romPath.c_str(), ROMBackend::LRU);
    if (!cart->isValid()) {
        displayManager.clearScreen();
        displayManager.setTextColor(TFT_RED);
        displayManager.printText("Invalid ROM / unsupported mapper");
        delay(2000);
        i2s_driver_uninstall(I2S_NUM_0);
        delete cart;
        return;
    }

    // Heap-allocate Bus (~6 KB) — must NOT be stack-allocated on the 8 KB main task.
    Bus* nes = new Bus();
    if (!nes) {
        displayManager.clearScreen();
        displayManager.setTextColor(TFT_RED);
        displayManager.printText("Out of memory");
        delay(2000);
        i2s_driver_uninstall(I2S_NUM_0);
        delete cart;
        return;
    }

    nes->connectDisplayFlush(nesFlush);
    nes->insertCartridge(cart);
    nes->reset();

    displayManager.setBlocked(true);
    tft.fillScreen(TFT_BLACK);

    // Idle-timeout / tpad-hold auto-lock is suppressed for the whole gm session
    // by runNesEmulator() (covers both the library and the game). Explicit lock()
    // calls (panic key, `lock` command) still fire and are handled by the loop below.

    // Brief controls reminder overlaid on the first ~1.5 s of play. [q] now
    // returns to the library, not the CLI — the toast says "Q=menu" so it's clear.
    showToast("Q=menu  K=B L=A  ENT=Start");

    // APU on core 0
    xTaskCreatePinnedToCore(apuTask, "NES_APU", NES_APU_STACK,
                            &nes->cpu.apu, 5, &s_apuTask, 0);

    // Emulation loop — core 1
    bool was_blocked = false;

    while (!s_nesQuit) {
        // Poll keyboard FIRST so LockScreenManager::intercept() fires before
        // any NES frame is drawn — prevents a one-frame NES+lockscreen flash.
        char k = inputHandler.getKeyboardInput();

        if (LockScreenManager::getInstance().isLocked()) {
            if (!was_blocked) {
                vTaskSuspend(s_apuTask);
                was_blocked = true;
            }
            delay(30);
            continue;
        }

        if (was_blocked) {
            vTaskResume(s_apuTask);
            tft.fillScreen(TFT_BLACK);
            was_blocked = false;
        }

        if (k == 'q') break;

        // Save / load state — [e] save, [r] load
        if (k == 'e') {
            nes->saveState();
            showToast("State saved  [e]=save [r]=load");
        }
        if (k == 'r') {
            nes->loadState();
            showToast("State loaded [e]=save [r]=load");
        }

        PowerSaveManager::getInstance().updateActivity();

        TrackballEvent tb = inputHandler.getTrackballEvent();
        nes->controller = ctrlUpdate(k, tb);

        // Sync master volume to APU every frame so `vol` takes effect live.
        nes->cpu.apu.volume = getMasterVolume();

        displayManager.setBlocked(false);
        nes->clock();
        drawToast();  // overlay on top of the NES frame while toast is active
        displayManager.setBlocked(true);   // re-block between frames so status bar can't draw

        vTaskDelay(1);
    }

    // Cleanup.
    if (s_apuTask) {
        vTaskDelete(s_apuTask);
        s_apuTask = nullptr;
    }
    i2s_driver_uninstall(I2S_NUM_0);
    // Belt-and-suspenders: re-assert the trackball-click pin as a clean input in
    // case the I2S driver disturbed GPIO0 (see mck_io_num note in nesI2sInit()),
    // so the lockscreen's trackpad-hold detector doesn't read a phantom LOW.
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    delete nes;
    delete cart;

    displayManager.setBlocked(false);
    tft.fillScreen(TFT_BLACK);
}

// ── main entry point ──────────────────────────────────────────────────────────
// gm [rom]. Bare `gm` opens the ROM library (pickRom); `gm <file>` boots straight
// into that ROM. After a game exits it loops BACK to the library rather than the
// CLI — [q] in the library is the only way out to the terminal.
void runNesEmulator(char* args)
{
    // Suppress idle-timeout + tpad-hold auto-lock for the whole session (library
    // browsing AND gameplay) so the screen can't lock us out mid-session or leave
    // a stale ROM-library screen after an unlock. Explicit lock()/panic still fire.
    // Restored once on final exit — which also resets _tpadHeld and stamps activity.
    LockScreenManager::getInstance().suppressAutoLock(true);

    // A CLI-supplied ROM is played first, then we fall through to the library.
    String initial;
    if (args && strlen(args) > 0) {
        initial = String(args);
        if (!initial.startsWith("/")) initial = String(SD_DIR_NES) + "/" + initial;
    }

    while (true) {
        String romPath;
        if (!initial.isEmpty()) {
            romPath = initial;
            initial = "";              // consume the CLI arg once
        } else {
            romPath = pickRom();       // the gm UI
        }
        if (romPath.isEmpty()) break;  // [q] in the library → back to the CLI

        runOneGame(romPath);
        // fall through: loop back into the ROM library
    }

    // Restore auto-lock + the CLI exactly once, on final exit. suppressAutoLock(false)
    // stamps _lastActivityMs and clears _tpadHeld so a stale in-game/library trackball
    // press can't immediately trigger the 3-s hold lock back at the terminal.
    LockScreenManager::getInstance().suppressAutoLock(false);
    displayManager.setBlocked(false);
    tft.fillScreen(TFT_BLACK);
    displayManager.tdeck_begin();
}
