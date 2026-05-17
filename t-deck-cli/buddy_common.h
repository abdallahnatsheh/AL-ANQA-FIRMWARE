// Shared constants and rendering declarations for ASCII pet species files.
// Each species file in buddies/*.cpp includes this header.
#pragma once
#include <stdint.h>

// Persona state indices (order must match PersonaState enum in buddy.cpp)
#define B_SLEEP     0
#define B_IDLE      1
#define B_BUSY      2
#define B_ATTENTION 3
#define B_CELEBRATE 4
#define B_DIZZY     5
#define B_HEART     6

// Geometry — all species files use these; defined in buddy.cpp
extern const int BUDDY_X_CENTER;
extern const int BUDDY_CANVAS_W;
extern const int BUDDY_Y_BASE;
extern const int BUDDY_Y_OVERLAY;
extern const int BUDDY_CHAR_W;
extern const int BUDDY_CHAR_H;

// Colors — defined in buddy.cpp
extern const uint16_t BUDDY_BG;
extern const uint16_t BUDDY_HEART;
extern const uint16_t BUDDY_DIM;
extern const uint16_t BUDDY_YEL;
extern const uint16_t BUDDY_WHITE;
extern const uint16_t BUDDY_CYAN;
extern const uint16_t BUDDY_GREEN;
extern const uint16_t BUDDY_PURPLE;
extern const uint16_t BUDDY_RED;
extern const uint16_t BUDDY_BLUE;

// Rendering helpers — implemented in buddy.cpp, called by every species
void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff = 0);
void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset, uint16_t color, int xOff = 0);
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char* s);

// Species descriptor — one per animal, defined in each buddies/*.cpp file
typedef void (*StateFn)(uint32_t t);
struct Species {
    const char* name;
    uint16_t    bodyColor;
    StateFn     states[7];   // indexed by B_SLEEP..B_HEART
};
