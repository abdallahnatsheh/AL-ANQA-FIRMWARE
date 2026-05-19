#include "powersave_manager.h"
#include "display_manager.h"
#include "battery_manager.h"
#include <SD.h>

// Static instance and config file path
PowerSaveManager* g_powerSaveInstance = nullptr;
const char* PowerSaveManager::CONFIG_FILE_PATH = "/pwrsave.conf";

extern DisplayManager displayManager;

PowerSaveManager& PowerSaveManager::getInstance() {
    if (!g_powerSaveInstance) {
        g_powerSaveInstance = new PowerSaveManager();
    }
    return *g_powerSaveInstance;
}

PowerSaveManager::PowerSaveManager()
    : inactivityTimeoutMs(DEFAULT_INACTIVITY_TIMEOUT_MS),
      screenOffTimeoutMs(DEFAULT_SCREEN_OFF_TIMEOUT_MS),
      dimBrightness(DEFAULT_DIM_BRIGHTNESS),
      fullBrightness(DEFAULT_FULL_BRIGHTNESS),
      batteryDimBrightness(DEFAULT_BATTERY_DIM_BRIGHTNESS),
      batteryThreshold(DEFAULT_BATTERY_THRESHOLD),
      enabled(DEFAULT_PWRSAVE_ENABLED),
      isDimState(false),
      isScreenOffState(false),
      batteryAwareDimEnabled(DEFAULT_BATTERY_MODE_ENABLED),
      screenOffEnabled(DEFAULT_SCREEN_OFF_ENABLED),
      _manualOff(false),
      lastActivityTime(millis()),
      lastBatteryPercent(-1),
      batteryModeActive(false),
      batteryManager(nullptr) {}

void PowerSaveManager::init(void* batteryManagerPtr) {
    batteryManager = batteryManagerPtr;
    lastActivityTime = millis();
    loadConfigFromSD();
    // Force full brightness on startup; wakeUp() skips when isDimState=false
    extern LGFX tft;
    tft.setBrightness(fullBrightness);
    isDimState = false;
    batteryModeActive = false;
}

void PowerSaveManager::update() {
    if (!enabled || _manualOff) return;
    
    uint32_t now = millis();
    
    // Check battery state if battery mode enabled (rate-limited to 1 Hz — getPct() reads ADC 20×)
    if (batteryAwareDimEnabled && batteryManager) {
        static uint32_t lastBatteryCheck = 0;
        if (now - lastBatteryCheck >= 1000) {
            lastBatteryCheck = now;
            BatteryManager* bm = static_cast<BatteryManager*>(batteryManager);
            int currentBattery = bm->getPct();
            if (currentBattery < (int)batteryThreshold && !batteryModeActive) {
                batteryModeActive = true;
                applyDim();
                return;
            } else if (currentBattery >= (int)batteryThreshold && batteryModeActive) {
                batteryModeActive = false;
                if (isDimState && now - lastActivityTime < inactivityTimeoutMs) {
                    wakeUp();
                }
                return;
            }
        }
    }

    // Normal inactivity-based dimming (only if battery mode not active)
    if (!batteryModeActive) {
        uint32_t elapsed = now - lastActivityTime;
        if (screenOffEnabled && elapsed >= screenOffTimeoutMs) {
            if (!isScreenOffState) applyScreenOff();
        } else if (elapsed >= inactivityTimeoutMs) {
            if (isScreenOffState) { isScreenOffState = false; }
            if (!isDimState) applyDim();
        } else {
            if (isDimState || isScreenOffState) wakeUp();
        }
    }
}

void PowerSaveManager::applyDim() {
    if (!isDimState) {
        uint8_t targetBrightness = batteryModeActive ? batteryDimBrightness : dimBrightness;
        extern LGFX tft;
        tft.setBrightness(targetBrightness);
        isDimState = true;
        isScreenOffState = false;
    }
}

void PowerSaveManager::applyScreenOff() {
    extern LGFX tft;
    tft.setBrightness(0);
    isDimState      = true;
    isScreenOffState = true;
}

void PowerSaveManager::wakeUp() {
    if (isDimState || isScreenOffState) {
        extern LGFX tft;
        tft.setBrightness(fullBrightness);
        isDimState       = false;
        isScreenOffState = false;
    }
    batteryModeActive = false;
}

void PowerSaveManager::updateActivity() {
    lastActivityTime = millis();
    if (_manualOff) {
        _manualOff = false;
        wakeUp();
        return;
    }
    if ((isDimState || isScreenOffState) && !batteryModeActive) {
        wakeUp();
    }
}

void PowerSaveManager::toggleManualOff() {
    if (_manualOff) {
        // Already off — turn back on
        _manualOff = false;
        lastActivityTime = millis();
        wakeUp();
    } else {
        // Turn screen off immediately
        _manualOff = true;
        extern LGFX tft;
        tft.setBrightness(0);
        isDimState       = true;
        isScreenOffState = true;
    }
}

void PowerSaveManager::setScreenOffTimeoutMs(uint32_t ms) {
    if (ms < MIN_TIMEOUT_MS) ms = MIN_TIMEOUT_MS;
    if (ms > MAX_TIMEOUT_MS) ms = MAX_TIMEOUT_MS;
    screenOffTimeoutMs = ms;
}

void PowerSaveManager::setTimeoutMs(uint32_t ms) {
    if (ms < MIN_TIMEOUT_MS) ms = MIN_TIMEOUT_MS;
    if (ms > MAX_TIMEOUT_MS) ms = MAX_TIMEOUT_MS;
    inactivityTimeoutMs = ms;
}

void PowerSaveManager::setDimBrightness(uint8_t brightness) {
    if (brightness > MAX_BRIGHTNESS) brightness = MAX_BRIGHTNESS;
    dimBrightness = brightness;
}

void PowerSaveManager::setFullBrightness(uint8_t brightness) {
    if (brightness > MAX_BRIGHTNESS) brightness = MAX_BRIGHTNESS;
    fullBrightness = brightness;
}

void PowerSaveManager::setBatteryDimBrightness(uint8_t brightness) {
    if (brightness > MAX_BRIGHTNESS) brightness = MAX_BRIGHTNESS;
    batteryDimBrightness = brightness;
}

void PowerSaveManager::setBatteryThreshold(uint8_t percent) {
    if (percent > MAX_BATTERY_THRESHOLD) percent = MAX_BATTERY_THRESHOLD;
    batteryThreshold = percent;
}

void PowerSaveManager::loadConfigFromSD() {
    if (!SD.exists(CONFIG_FILE_PATH)) {
        // Config doesn't exist, use defaults
        return;
    }
    
    File configFile = SD.open(CONFIG_FILE_PATH, FILE_READ);
    if (!configFile) {
        return;
    }
    
    // Simple line-based config format (not full JSON for simplicity)
    // Format:
    // enabled=true/false
    // timeoutMs=value
    // dimBrightness=value
    // fullBrightness=value
    // batteryAwareDimEnabled=true/false
    // batteryThreshold=value
    // batteryDimBrightness=value
    
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0 || line[0] == '#') continue;
        
        int eqPos = line.indexOf('=');
        if (eqPos == -1) continue;
        
        String key = line.substring(0, eqPos);
        String value = line.substring(eqPos + 1);
        key.trim();
        value.trim();
        
        if (key == "enabled") {
            enabled = (value == "true" || value == "1");
        } else if (key == "timeoutMs") {
            inactivityTimeoutMs = value.toInt();
        } else if (key == "screenOffTimeoutMs") {
            screenOffTimeoutMs = value.toInt();
        } else if (key == "screenOffEnabled") {
            screenOffEnabled = (value == "true" || value == "1");
        } else if (key == "dimBrightness") {
            dimBrightness = value.toInt();
        } else if (key == "fullBrightness") {
            fullBrightness = value.toInt();
        } else if (key == "batteryAwareDimEnabled") {
            batteryAwareDimEnabled = (value == "true" || value == "1");
        } else if (key == "batteryThreshold") {
            batteryThreshold = value.toInt();
        } else if (key == "batteryDimBrightness") {
            batteryDimBrightness = value.toInt();
        }
    }
    
    configFile.close();
}

void PowerSaveManager::saveConfigToSD() {
    if (SD.exists(CONFIG_FILE_PATH)) {
        SD.remove(CONFIG_FILE_PATH);
    }
    File configFile = SD.open(CONFIG_FILE_PATH, FILE_WRITE);
    if (!configFile) {
        displayManager.println("Error: Failed to save config to SD");
        displayManager.printCommandScreen();
        return;
    }
    
    // Write config in simple key=value format
    configFile.print("enabled=");
    configFile.println(enabled ? "true" : "false");
    
    configFile.print("timeoutMs=");
    configFile.println(inactivityTimeoutMs);

    configFile.print("screenOffTimeoutMs=");
    configFile.println(screenOffTimeoutMs);

    configFile.print("screenOffEnabled=");
    configFile.println(screenOffEnabled ? "true" : "false");

    configFile.print("dimBrightness=");
    configFile.println(dimBrightness);
    
    configFile.print("fullBrightness=");
    configFile.println(fullBrightness);
    
    configFile.print("batteryAwareDimEnabled=");
    configFile.println(batteryAwareDimEnabled ? "true" : "false");
    
    configFile.print("batteryThreshold=");
    configFile.println(batteryThreshold);
    
    configFile.print("batteryDimBrightness=");
    configFile.println(batteryDimBrightness);
    
    configFile.close();
    
    displayManager.println("PowerSave config saved to SD.");
    displayManager.printCommandScreen();
}

void PowerSaveManager::resetToDefaults() {
    enabled = DEFAULT_PWRSAVE_ENABLED;
    inactivityTimeoutMs = DEFAULT_INACTIVITY_TIMEOUT_MS;
    screenOffTimeoutMs  = DEFAULT_SCREEN_OFF_TIMEOUT_MS;
    screenOffEnabled    = DEFAULT_SCREEN_OFF_ENABLED;
    dimBrightness = DEFAULT_DIM_BRIGHTNESS;
    fullBrightness = DEFAULT_FULL_BRIGHTNESS;
    batteryDimBrightness = DEFAULT_BATTERY_DIM_BRIGHTNESS;
    batteryThreshold = DEFAULT_BATTERY_THRESHOLD;
    batteryAwareDimEnabled = DEFAULT_BATTERY_MODE_ENABLED;
    
    // Delete config file from SD
    if (SD.exists(CONFIG_FILE_PATH)) {
        SD.remove(CONFIG_FILE_PATH);
        displayManager.println("Config file deleted.");
    }
    
    wakeUp();
    displayManager.println("PowerSave reset to defaults.");
    displayManager.printCommandScreen();
}

static void psvLabel(const char* label) {
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.printText(label);
}

void PowerSaveManager::printStatus() {
    char buf[40];

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("Power Save");
    displayManager.setTextColor(0x7BEF);
    displayManager.println("-------------------------------");

    psvLabel("State    ");
    displayManager.setTextColor(enabled ? TFT_GREEN : TFT_RED);
    displayManager.println(enabled ? "ON" : "OFF");

    if (batteryManager) {
        BatteryManager* bm = static_cast<BatteryManager*>(batteryManager);
        int pct = bm->getPct();
        psvLabel("Battery  ");
        uint16_t bc = pct >= 60 ? TFT_GREEN : (pct >= 30 ? TFT_YELLOW : TFT_RED);
        displayManager.setTextColor(bc);
        snprintf(buf, sizeof(buf), "%d%%", pct);
        displayManager.println(buf);
    }

    psvLabel("Dim @    ");
    displayManager.setTextColor(TFT_WHITE);
    snprintf(buf, sizeof(buf), "%us", (unsigned int)(inactivityTimeoutMs / 1000));
    displayManager.println(buf);

    psvLabel("Off @    ");
    displayManager.setTextColor(screenOffEnabled ? TFT_WHITE : 0x7BEF);
    snprintf(buf, sizeof(buf), screenOffEnabled ? "%us" : "%us (off)",
             (unsigned int)(screenOffTimeoutMs / 1000));
    displayManager.println(buf);

    psvLabel("Dim/Full ");
    displayManager.setTextColor(TFT_WHITE);
    snprintf(buf, sizeof(buf), "%u / %u", dimBrightness, fullBrightness);
    displayManager.println(buf);

    psvLabel("Screen   ");
    if (isScreenOffState) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("OFF");
    } else if (isDimState) {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("DIM");
    } else {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("ON");
    }

    displayManager.setTextColor(0x7BEF);
    displayManager.println("-------------------------------");

    psvLabel("Batt Mode ");
    displayManager.setTextColor(batteryAwareDimEnabled ? TFT_GREEN : TFT_RED);
    displayManager.println(batteryAwareDimEnabled ? "ON" : "OFF");

    if (batteryAwareDimEnabled) {
        psvLabel("Threshold ");
        displayManager.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "%u%%", batteryThreshold);
        displayManager.println(buf);

        psvLabel("Batt Dim  ");
        displayManager.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "%u", batteryDimBrightness);
        displayManager.println(buf);
    }
}

void PowerSaveManager::handleCommand(char* args) {
    PowerSaveManager& psm = getInstance();

    if (!args || *args == '\0' || strcmp(args, "status") == 0) {
        psm.printStatus();
        displayManager.printCommandScreen();
        return;
    }
    
    if (strcmp(args, "on") == 0) {
        psm.enable();
        displayManager.println("PowerSave: ON");
        displayManager.printCommandScreen();
        return;
    }
    
    if (strcmp(args, "off") == 0) {
        psm.disable();
        displayManager.println("PowerSave: OFF");
        displayManager.printCommandScreen();
        return;
    }
    
    if (strncmp(args, "set ", 4) == 0) {
        char* subcmd = args + 4;
        
        // Set timeout
        if (strncmp(subcmd, "timeout ", 8) == 0) {
            uint32_t ms = (uint32_t)atoi(subcmd + 8) * 1000;
            psm.setTimeoutMs(ms);
            char buf[32];
            snprintf(buf, sizeof(buf), "Timeout set to %us", (unsigned int)(ms / 1000));
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }

        // Set dim brightness
        if (strncmp(subcmd, "dimto ", 6) == 0) {
            uint8_t br = (uint8_t)atoi(subcmd + 6);
            psm.setDimBrightness(br);
            char buf[32];
            snprintf(buf, sizeof(buf), "Dim level set to %u", br);
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }

        // Set full brightness
        if (strncmp(subcmd, "fullto ", 7) == 0) {
            uint8_t br = (uint8_t)atoi(subcmd + 7);
            psm.setFullBrightness(br);
            char buf[32];
            snprintf(buf, sizeof(buf), "Full level set to %u", br);
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }
        
        // Set screen-off timeout
        if (strncmp(subcmd, "screenoff ", 10) == 0) {
            uint32_t ms = (uint32_t)atoi(subcmd + 10) * 1000;
            psm.setScreenOffTimeoutMs(ms);
            char buf[36];
            snprintf(buf, sizeof(buf), "Screen-off @ %us", (unsigned int)(ms / 1000));
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }

        // Enable/disable screen-off
        if (strncmp(subcmd, "screenoffmode ", 14) == 0) {
            const char* mode = subcmd + 14;
            if (strcmp(mode, "on") == 0) {
                psm.enableScreenOff();
                displayManager.println("Screen-off: ON");
            } else if (strcmp(mode, "off") == 0) {
                psm.disableScreenOff();
                displayManager.println("Screen-off: OFF");
            } else {
                displayManager.println("Use: on or off");
            }
            displayManager.printCommandScreen();
            return;
        }

        // Set battery mode
        if (strncmp(subcmd, "batterymode ", 12) == 0) {
            char* mode = subcmd + 12;
            if (strcmp(mode, "on") == 0) {
                psm.enableBatteryMode();
                displayManager.println("Battery mode: ON");
            } else if (strcmp(mode, "off") == 0) {
                psm.disableBatteryMode();
                displayManager.println("Battery mode: OFF");
            } else {
                displayManager.println("Invalid mode. Use 'on' or 'off'");
            }
            displayManager.printCommandScreen();
            return;
        }
        
        // Set battery threshold
        if (strncmp(subcmd, "batterythreshold ", 17) == 0) {
            uint8_t pct = (uint8_t)atoi(subcmd + 17);
            psm.setBatteryThreshold(pct);
            char buf[32];
            snprintf(buf, sizeof(buf), "Batt threshold: %u%%", pct);
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }

        // Set battery dim brightness
        if (strncmp(subcmd, "batterydimu ", 12) == 0) {
            uint8_t br = (uint8_t)atoi(subcmd + 12);
            psm.setBatteryDimBrightness(br);
            char buf[32];
            snprintf(buf, sizeof(buf), "Batt dim level: %u", br);
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }
        
        displayManager.println("Unknown set command");
        displayManager.printCommandScreen();
        return;
    }
    
    if (strcmp(args, "save") == 0) {
        psm.saveConfigToSD();
        return;
    }
    
    if (strcmp(args, "reset") == 0) {
        psm.resetToDefaults();
        return;
    }
    
    // Invalid command
    displayManager.println("PowerSave Commands:");
    displayManager.println("  status - Show current status");
    displayManager.println("  on/off - Enable/disable");
    displayManager.println("  set timeout <sec>");
    displayManager.println("  set dimto <0-255>");
    displayManager.println("  set fullto <0-255>");
    displayManager.println("  set batterymode on|off");
    displayManager.println("  set batterythreshold <%>");
    displayManager.println("  set batterydimu <0-255>");
    displayManager.println("  save - Save to SD");
    displayManager.println("  reset - Reset to defaults");
    displayManager.printCommandScreen();
}
