#include "touch_test.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "touch_manager.h"
#include <stdio.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── Layout (fixed pixel positions for partial redraws) ──────────────────────
#define Y_HEADER  (outputY)
#define Y_STATUS  (Y_HEADER + LINE_HEIGHT * 2)
#define Y_COORD   (Y_STATUS + LINE_HEIGHT)
#define Y_EVENT   (Y_COORD + LINE_HEIGHT)
#define Y_FOOTER  (SCREEN_HEIGHT - LINE_HEIGHT - 2)

#define TT_TOP    (Y_EVENT + LINE_HEIGHT + 6)
#define TT_BOTTOM (Y_FOOTER - 6)
#define TT_LEFT   4
#define TT_RIGHT  (SCREEN_WIDTH - 4)
#define TT_BRACKET 8

static const char* eventName(TouchEvent::Type t) {
    switch (t) {
        case TouchEvent::TAP:        return "TAP";
        case TouchEvent::LONG_PRESS: return "LONG_PRESS";
        case TouchEvent::DRAG_START: return "DRAG_START";
        case TouchEvent::DRAG_MOVE:  return "DRAG_MOVE";
        case TouchEvent::DRAG_END:   return "DRAG_END";
        default:                     return "-";
    }
}

// (x,y) is the corner anchor; dx/dy = +1 if the bar extends right/down from
// it, -1 if it extends left/up. fillRect takes unsigned w/h — never pass it
// a negative length, offset the origin instead.
static void drawCornerBracket(int16_t x, int16_t y, int8_t dx, int8_t dy) {
    int16_t hx = (dx > 0) ? x : (x - TT_BRACKET + 1);
    int16_t vy = (dy > 0) ? y : (y - TT_BRACKET + 1);
    displayManager.fillRect(hx, y, TT_BRACKET, 1, 0x4208);
    displayManager.fillRect(x, vy, 1, TT_BRACKET, 0x4208);
}

static void drawStaticUI() {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, Y_HEADER);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("TOUCH");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("TEST");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    TouchManager& tm = TouchManager::instance();
    if (tm.isPresent()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "GT911 @ 0x%02X: OK", tm.address());
        dm.printText(buf, 4, Y_STATUS, TFT_GREEN);
    } else {
        dm.printText("GT911: NOT FOUND", 4, Y_STATUS, TFT_RED);
    }

    // Corner brackets mark the tracking test area — touch should follow the
    // finger 1:1 into each one with no mirror/swap (Phase 0 acceptance).
    drawCornerBracket(TT_LEFT,  TT_TOP,    1,  1);
    drawCornerBracket(TT_RIGHT, TT_TOP,   -1,  1);
    drawCornerBracket(TT_LEFT,  TT_BOTTOM, 1, -1);
    drawCornerBracket(TT_RIGHT, TT_BOTTOM,-1, -1);

    dm.printText("[q] quit", 4, Y_FOOTER, 0x7BEF);
}

static void drawCoords(int16_t x, int16_t y) {
    char buf[32];
    snprintf(buf, sizeof(buf), "x=%3d y=%3d", x, y);
    displayManager.fillRect(4, Y_COORD, 200, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(buf, 4, Y_COORD, TFT_WHITE);
}

static void drawEvent(const char* name) {
    char buf[32];
    snprintf(buf, sizeof(buf), "event: %s", name);
    displayManager.fillRect(4, Y_EVENT, 250, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(buf, 4, Y_EVENT, TFT_YELLOW);
}

void runTouchTest() {
    TouchManager& tm = TouchManager::instance();
    drawStaticUI();

    int16_t lastDotX = -1, lastDotY = -1;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawStaticUI();
            lastDotX = lastDotY = -1;
        }

        TouchEvent ev = tm.poll();
        if (ev.type != TouchEvent::NONE) {
            drawCoords(ev.x, ev.y);
            drawEvent(eventName(ev.type));

            if (ev.y >= TT_TOP && ev.y <= TT_BOTTOM && ev.x >= TT_LEFT && ev.x <= TT_RIGHT) {
                // fillRect takes unsigned x/y — clamp 4px in so x-4/y-4 never underflows.
                int16_t dx = ev.x < 4 ? 4 : ev.x;
                int16_t dy = ev.y < 4 ? 4 : ev.y;
                if (lastDotX >= 0)
                    displayManager.fillRect(lastDotX - 4, lastDotY - 4, 9, 9, TFT_BLACK);
                displayManager.fillRect(dx - 4, dy, 9, 1, TFT_CYAN);
                displayManager.fillRect(dx, dy - 4, 1, 9, TFT_CYAN);
                lastDotX = dx;
                lastDotY = dy;
            }
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.printCommandScreen();
}
