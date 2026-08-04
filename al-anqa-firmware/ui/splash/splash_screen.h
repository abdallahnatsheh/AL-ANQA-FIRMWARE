#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

// holdUntilKey=false: boot behaviour — show ~3s or until a key is pressed.
// holdUntilKey=true:  stay on screen until the user presses any key (no timeout).
void showSplashScreen(bool holdUntilKey = false);

#endif
