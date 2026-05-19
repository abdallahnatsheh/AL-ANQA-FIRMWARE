#ifndef POWERSAVE_MANAGER_H
#define POWERSAVE_MANAGER_H

#include <Arduino.h>
#include <SD.h>

// Default power-save settings
#define DEFAULT_INACTIVITY_TIMEOUT_MS   120000  // 2 minutes
#define DEFAULT_SCREEN_OFF_TIMEOUT_MS   300000  // 5 minutes
#define DEFAULT_DIM_BRIGHTNESS          32
#define DEFAULT_FULL_BRIGHTNESS         128
#define DEFAULT_BATTERY_DIM_BRIGHTNESS  30
#define DEFAULT_BATTERY_THRESHOLD       20      // 20%
#define DEFAULT_BATTERY_MODE_ENABLED    true
#define DEFAULT_PWRSAVE_ENABLED         true
#define DEFAULT_SCREEN_OFF_ENABLED      true

// Min/Max constraints
#define MIN_TIMEOUT_MS      10000       // 10 seconds
#define MAX_TIMEOUT_MS      3600000     // 1 hour
#define MIN_BRIGHTNESS      0
#define MAX_BRIGHTNESS      255
#define MIN_BATTERY_THRESHOLD 0
#define MAX_BATTERY_THRESHOLD 100

class PowerSaveManager {
public:
    static PowerSaveManager& getInstance();
    
    void init(void* batteryManagerPtr);
    void update();
    
    // Status
    bool isEnabled() const { return enabled; }
    bool isDimmed() const { return isDimState; }
    bool isBatteryModeEnabled() const { return batteryAwareDimEnabled; }
    uint32_t getTimeoutMs() const { return inactivityTimeoutMs; }
    uint8_t getDimBrightness() const { return dimBrightness; }
    uint8_t getFullBrightness() const { return fullBrightness; }
    uint8_t getBatteryDimBrightness() const { return batteryDimBrightness; }
    uint8_t getBatteryThreshold() const { return batteryThreshold; }
    
    // Control
    void enable() { enabled = true; }
    void disable() { enabled = false; wakeUp(); }
    void enableBatteryMode() { batteryAwareDimEnabled = true; }
    void disableBatteryMode() { batteryAwareDimEnabled = false; }
    void enableScreenOff()  { screenOffEnabled = true; }
    void disableScreenOff() { screenOffEnabled = false; }
    bool isScreenOff() const { return isScreenOffState; }
    bool isManualOff() const { return _manualOff; }
    void updateActivity();    // Called when user provides input
    void toggleManualOff();  // Double-click: toggle screen off/on

    // Settings modification
    void setTimeoutMs(uint32_t ms);
    void setScreenOffTimeoutMs(uint32_t ms);
    void setDimBrightness(uint8_t brightness);
    void setFullBrightness(uint8_t brightness);
    void setBatteryDimBrightness(uint8_t brightness);
    void setBatteryThreshold(uint8_t percent);
    
    // SD persistence
    void loadConfigFromSD();
    void saveConfigToSD();
    void resetToDefaults();
    
    // UI info
    void printStatus();
    
    // Command handler
    static void handleCommand(char* args);

private:
    PowerSaveManager();
    ~PowerSaveManager() = default;
    
    // Prevent copy
    PowerSaveManager(const PowerSaveManager&) = delete;
    PowerSaveManager& operator=(const PowerSaveManager&) = delete;
    
    void applyDim();
    void applyScreenOff();
    void wakeUp();
    void updateFromBatteryState();

    // Config values
    uint32_t inactivityTimeoutMs;
    uint32_t screenOffTimeoutMs;
    uint8_t  dimBrightness;
    uint8_t  fullBrightness;
    uint8_t  batteryDimBrightness;
    uint8_t  batteryThreshold;

    // Flags
    bool enabled;
    bool isDimState;
    bool isScreenOffState;
    bool batteryAwareDimEnabled;
    bool screenOffEnabled;
    bool _manualOff;
    
    // Tracking
    uint32_t lastActivityTime;
    int      lastBatteryPercent;
    bool     batteryModeActive;
    
    // External dependencies
    void* batteryManager;  // Pointer to BatteryManager to avoid circular dependency
    
    // Configuration file path
    static const char* CONFIG_FILE_PATH;
};

#endif // POWERSAVE_MANAGER_H
