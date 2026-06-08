#include "../buddy.h"
#include "../buddy_common.h"
#include <M5StickCPlus.h>
#include <string.h>

extern TFT_eSprite spr;

// T-Rex face — 7 lines (112 px at scale 2) for a tall rectangular skull.
//   0  ".__________."   flat skull top
//   1  "|          |"   forehead
//   2  "| o      o |"   eyes
//   3  "|==========|"   brow ridge  (= not _ — visible at scale 2)
//   4  "| \\/ \\/ \\/ |"   upper teeth
//   5  "|  /\\  /\\  |"   lower teeth
//   6  "|          |"   open jaw bottom
//
//  Wide jaw: "|VVVVVVVVVV|" / "|/\\/\\/\\/\\/\\/\\|"
//  Sealed  : "|----------|" (dashes visible) or "|\\//\\\\//\\\\/|" (meshed)
namespace trex {

// ─── SLEEP ─────────────────────────────────────────────────────────────────
static void doSleep(uint32_t t) {
  static const char* const SNOOZE[7] = { ".__________.", "|          |", "| -      - |", "|==========|", "|----------|", "|----------|", "|          |" };
  static const char* const BREATH[7] = { ".__________.", "|          |", "| -      - |", "|==========|", "|\\//\\\\//\\\\/|", "|          |", "|          |" };
  static const char* const DREAM[7]  = { ".__________.", "|          |", "| ~      ~ |", "|==========|", "|----------|", "|----------|", "|          |" };

  const char* const* P[3] = { SNOOZE, BREATH, DREAM };
  static const uint8_t SEQ[] = { 0,0,1,0,0,1,0,2,0,1,0,0,1,0,2,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, 0, 0x07E0);

  int p1 = t % 10;
  int p2 = (t + 4) % 10;
  buddySetColor(BUDDY_DIM);
  buddySetCursor(BUDDY_X_CENTER + 16 + p1, BUDDY_Y_OVERLAY + 14 - p1 * 2);
  buddyPrint("z");
  buddySetColor(BUDDY_WHITE);
  buddySetCursor(BUDDY_X_CENTER + 22 + p2, BUDDY_Y_OVERLAY + 8 - p2);
  buddyPrint("Z");
}

// ─── IDLE ──────────────────────────────────────────────────────────────────
static void doIdle(uint32_t t) {
  static const char* const NEUTRAL[7] = { ".__________.", "|          |", "| o      o |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const BLINK[7]   = { ".__________.", "|          |", "| -      - |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const LOOK_L[7]  = { ".__________.", "|          |", "|o      o  |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const LOOK_R[7]  = { ".__________.", "|          |", "|  o      o|", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const SNIFF[7]   = { "     ~      ", ".__________.", "|          |", "| o      o |", "|==========|", "| \\/ \\/ \\/ |", "|          |" };

  const char* const* P[5] = { NEUTRAL, BLINK, LOOK_L, LOOK_R, SNIFF };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,0,1,0, 3,0,1,0,
    0,0,4,4, 0,1,0,3, 0,0,1,0, 2,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, 0, 0x07E0);
}

// ─── BUSY ──────────────────────────────────────────────────────────────────
static void doBusy(uint32_t t) {
  static const char* const THINK[7]  = { "   ? ? ?    ", ".__________.", "|          |", "| ^      ^ |", "|==========|", "| \\/ \\/ \\/ |", "|          |" };
  static const char* const PONDER[7] = { "   . . .    ", ".__________.", "|          |", "| ^      ^ |", "|==========|", "| \\/ \\/ \\/ |", "|          |" };
  static const char* const FOCUS[7]  = { ".__________.", "|          |", "| >      < |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const EUREKA[7] = { "    *!*     ", ".__________.", "|          |", "| O      O |", "|==========|", "| \\/ \\/ \\/ |", "|          |" };

  const char* const* P[4] = { THINK, PONDER, FOCUS, EUREKA };
  static const uint8_t SEQ[] = { 0,0,1,1, 2,2, 0,1, 2,2, 3,3, 0,1, 2,2 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, 0, 0x07E0);

  if (SEQ[beat] == 0 || SEQ[beat] == 1) {
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER + 20, BUDDY_Y_OVERLAY - 2 - (int)(t % 4));
    buddyPrint(".");
  }
}

// ─── ATTENTION ─────────────────────────────────────────────────────────────
static void doAttention(uint32_t t) {
  static const char* const ALERT[7]  = { ".__________.", "|          |", "| O      O |", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "|          |" };
  static const char* const ROAR[7]   = { ".__________.", "|          |", "| O      O |", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "            " };
  static const char* const SCAN_L[7] = { ".__________.", "|          |", "|O      O  |", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "|          |" };
  static const char* const SCAN_R[7] = { ".__________.", "|          |", "|  O      O|", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "|          |" };

  const char* const* P[4] = { ALERT, ROAR, SCAN_L, SCAN_R };
  static const uint8_t SEQ[] = { 0,1,0,2,0,3,0,1,0,2,0,3,0,1 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  int xOff = (SEQ[beat] <= 1) ? ((t & 1) ? 1 : -1) : 0;
  buddyPrintSprite(P[SEQ[beat]], 7, 0, 0x07E0, xOff);

  if ((t / 2) & 1) {
    buddySetColor(BUDDY_YEL);
    buddySetCursor(BUDDY_X_CENTER - 6, BUDDY_Y_OVERLAY - 2);
    buddyPrint("!");
  }
  if ((t / 3) & 1) {
    buddySetColor(BUDDY_RED);
    buddySetCursor(BUDDY_X_CENTER + 8, BUDDY_Y_OVERLAY + 2);
    buddyPrint("!");
  }
}

// ─── CELEBRATE ─────────────────────────────────────────────────────────────
static void doCelebrate(uint32_t t) {
  static const char* const GRIN[7]   = { "            ", ".__________.", "|          |", "| ^      ^ |", "|==========|", "| \\/ \\/ \\/ |", "|          |" };
  static const char* const JUMP[7]   = { ".__________.", "|          |", "| *      * |", "|==========|", "| \\/ \\/ \\/ |", "            ", "            " };
  static const char* const PEAK[7]   = { ".__________.", "|          |", "| *      * |", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "            " };
  static const char* const SPIN[7]   = { ".__________.", "|          |", "| <      > |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };

  const char* const* P[4] = { GRIN, JUMP, PEAK, SPIN };
  static const uint8_t SEQ[]    = { 0,1,2,1,0, 3,3, 0,1,2,1,0 };
  static const int8_t Y_SHIFT[] = { 0,-3,-6,-3,0, 0,0, 0,-3,-6,-3,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, Y_SHIFT[beat], 0x07E0);

  static const uint16_t cols[] = { BUDDY_YEL, BUDDY_HEART, BUDDY_CYAN, BUDDY_WHITE, BUDDY_GREEN };
  for (int i = 0; i < 6; i++) {
    int phase = (t * 2 + i * 11) % 22;
    int x = BUDDY_X_CENTER - 36 + i * 14;
    int y = BUDDY_Y_OVERLAY - 6 + phase;
    if (y > BUDDY_Y_BASE + 20 || y < 0) continue;
    buddySetColor(cols[i % 5]);
    buddySetCursor(x, y);
    buddyPrint((i + (int)(t / 2)) & 1 ? "*" : ".");
  }
}

// ─── DIZZY ─────────────────────────────────────────────────────────────────
static void doDizzy(uint32_t t) {
  static const char* const TILT_L[7]  = { ".__________.", "|          |", "| x      @ |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const TILT_R[7]  = { ".__________.", "|          |", "| @      x |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const WOOZY[7]   = { ".__________.", "|          |", "| x      x |", "|==========|", "|\\//\\\\//\\\\/|", "|          |", "|          |" };
  static const char* const STUMBLE[7] = { ".__________.", "|          |", "| @      @ |", "|==========|", "|VVVVVVVVVV|", "|/\\/\\/\\/\\/\\/\\|", "|          |" };

  const char* const* P[4] = { TILT_L, TILT_R, WOOZY, STUMBLE };
  static const uint8_t SEQ[]    = { 0,1,0,1, 2,2, 0,1,0,1, 3,3, 2,2 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, 0, 0x07E0, X_SHIFT[beat]);

  static const int8_t OX[] = { 0, 5, 7, 5, 0,-5,-7,-5 };
  static const int8_t OY[] = {-5,-3, 0, 3, 5, 3, 0,-3 };
  uint8_t p1 = t % 8;
  uint8_t p2 = (t + 4) % 8;
  buddySetColor(BUDDY_CYAN);
  buddySetCursor(BUDDY_X_CENTER + OX[p1] - 2, BUDDY_Y_OVERLAY + 6 + OY[p1]);
  buddyPrint("*");
  buddySetColor(BUDDY_YEL);
  buddySetCursor(BUDDY_X_CENTER + OX[p2] - 2, BUDDY_Y_OVERLAY + 6 + OY[p2]);
  buddyPrint("*");
}

// ─── HEART ─────────────────────────────────────────────────────────────────
static void doHeart(uint32_t t) {
  static const char* const LOVE[7]   = { ".__________.", "|          |", "|<3      <3|", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const BLUSH[7]  = { ".__________.", "|          |", "|#^      ^#|", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };
  static const char* const DREAMY[7] = { ".__________.", "|          |", "| ^      ^ |", "|==========|", "| \\/ \\/ \\/ |", "|  /\\  /\\  |", "|          |" };

  const char* const* P[3] = { LOVE, BLUSH, DREAMY };
  static const uint8_t SEQ[]  = { 0,0,1,0, 2,2,0, 1,0,0, 2,0,1,0 };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,-1 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  buddyPrintSprite(P[SEQ[beat]], 7, Y_BOB[beat], 0x07E0);

  buddySetColor(BUDDY_HEART);
  for (int i = 0; i < 4; i++) {
    int phase = (t + i * 4) % 16;
    int y = BUDDY_Y_OVERLAY + 16 - phase;
    if (y < -2 || y > BUDDY_Y_BASE) continue;
    int x = BUDDY_X_CENTER - 16 + i * 8 + ((phase / 3) & 1) * 2 - 1;
    buddySetCursor(x, y);
    buddyPrint("V");
  }
}

}  // namespace trex

extern const Species TREX_SPECIES = {
  "t-rex",
  0x07E0,
  { trex::doSleep, trex::doIdle, trex::doBusy, trex::doAttention,
    trex::doCelebrate, trex::doDizzy, trex::doHeart }
};
