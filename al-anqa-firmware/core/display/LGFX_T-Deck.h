/**
 * @file   LGFX_T-Deck.h
 * @brief  Backward-compat forwarder — the LovyanGFX panel config moved into the
 *         board layer (core/board/<variant>/lgfx_panel.h) and is selected here
 *         by the -DBOARD_* build flag.
 *
 * Kept under the original name so display_manager.h / esp_info.cpp /
 * splash_screen.cpp include it unchanged. On the T-Deck this expands to the
 * exact same `class LGFX` as before.
 */
#pragma once

#if defined(BOARD_TPAGER)
  #include "tpager/lgfx_panel.h"
#else
  #include "tdeck/lgfx_panel.h"
#endif
