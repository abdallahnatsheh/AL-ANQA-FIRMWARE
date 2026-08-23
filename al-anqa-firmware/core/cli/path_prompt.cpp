#include "path_prompt.h"
#include <Arduino.h>
#include <string.h>
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "completion_util.h"   // shared longest-common-prefix (also used by command_manager)

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

namespace pathprompt {

static void draw(const char* label, const char* out) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);  dm.println(label);
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
    char line[200]; snprintf(line, sizeof(line), "> %s_", out); dm.println(line);
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.println("' complete  Enter ok  click/empty-Enter cancel");
}

bool prompt(const char* label, char* out, size_t cap) {
    if (cap == 0) return false;
    out[0] = '\0';
    int  len = 0;
    bool redraw = true;

    for (;;) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        if (redraw && !displayManager.isBlocked()) { draw(label, out); redraw = false; }

        char k = inputHandler.getKeyboardInput();           // still pumped while locked (drives PIN entry)
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (displayManager.isBlocked()) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }  // lock screen owns the display

        if (e == TBALL_CLICK) return false;                 // cancel
        if (k == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (k == '\r' || k == '\n') return len > 0;         // Enter confirms; Enter on an empty line = cancel
        if (k == '\b') { if (len > 0) { out[--len] = '\0'; redraw = true; } continue; }

        if (k == KEY_AUTOCOMPLETE) {                         // ' → complete the path segment
            // split `out` into <dir>/<prefix> at the last '/'
            char dir[160], pre[96];
            const char* slash = strrchr(out, '/');
            if (slash) {
                int dlen = (int)(slash - out);
                if (dlen > (int)sizeof(dir) - 1) dlen = (int)sizeof(dir) - 1;  // clamp to dir[] (bound not tied to caller's cap)
                if (dlen == 0) strcpy(dir, "/");
                else { strncpy(dir, out, dlen); dir[dlen] = '\0'; }
                strncpy(pre, slash + 1, sizeof(pre) - 1); pre[sizeof(pre) - 1] = '\0';
            } else {
                strncpy(dir, sdCardManager.getCwd(), sizeof(dir) - 1); dir[sizeof(dir) - 1] = '\0';
                strncpy(pre, out, sizeof(pre) - 1); pre[sizeof(pre) - 1] = '\0';
            }
            char rdir[160]; sdCardManager.resolvePath(dir, rdir, sizeof(rdir));
            char matches[16][64];
            int  n    = sdCardManager.listCompletions(rdir, pre, matches, 16, false, false);
            int  base = slash ? (int)(slash + 1 - out) : 0;  // where the prefix starts in `out`

            if (n == 1) {                                    // unique → fill it in (dirs carry a trailing '/')
                snprintf(out + base, cap - base, "%s", matches[0]);
                len = (int)strlen(out); redraw = true;
            } else if (n > 1) {                              // extend to the common prefix + list options
                int cl = completion::commonPrefixLen<64>(matches, n);
                if (cl > (int)strlen(pre) && base + cl < (int)cap) {
                    strncpy(out + base, matches[0], cl); out[base + cl] = '\0';
                    len = (int)strlen(out);
                }
                draw(label, out);
                DisplayManager& dm = displayManager;
                dm.setTextColor(0x4208);
                for (int i = 0; i < n && i < 8; i++) {
                    dm.setCursor(4, dm.getCursorY());
                    char l[44]; snprintf(l, sizeof(l), "%.40s", matches[i]); dm.println(l);
                }
                // leave the listing on screen until the next keystroke
            }
            continue;
        }

        if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7f && len < (int)cap - 1) {
            out[len++] = k; out[len] = '\0'; redraw = true;
        }
    }
}

} // namespace pathprompt
