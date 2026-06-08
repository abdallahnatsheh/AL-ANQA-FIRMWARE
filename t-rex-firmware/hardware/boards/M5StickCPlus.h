// Compatibility shim — lets claude-desktop-buddy species files compile on T-DECK.
// Species files do: #include <M5StickCPlus.h> + extern TFT_eSprite spr
// We satisfy both by typedef-ing LGFX_Sprite as TFT_eSprite.
#pragma once
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
typedef LGFX_Sprite TFT_eSprite;
