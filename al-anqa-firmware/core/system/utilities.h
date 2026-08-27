/**
 * @file   utilities.h
 * @brief  Backward-compat forwarder — pin definitions moved into the board layer.
 *
 * All hardware pin macros now live in core/board/<variant>/pins.h and are
 * selected by core/board/board.h from the -DBOARD_* build flag. This header is
 * kept so the ~27 files that #include "utilities.h" continue to work unchanged;
 * on the T-Deck the resulting macro set is byte-for-byte identical to before.
 *
 * New code should prefer including "board.h" directly.
 */
#pragma once

#include "board.h"
