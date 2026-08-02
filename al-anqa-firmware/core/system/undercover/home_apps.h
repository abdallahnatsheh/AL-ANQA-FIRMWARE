// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_apps — umbrella header pulling in every launcher app class. Each app now
// lives in its own app_<name>.{h,cpp} (one class per file); this header just lets
// the launcher (home_ui.cpp) include them all with a single #include.

#ifndef HOME_APPS_H
#define HOME_APPS_H

#include "app_calculator.h"
#include "app_clock.h"
#include "app_reminders.h"
#include "app_weather.h"
#include "app_calendar.h"
#include "app_flashlight.h"
#include "app_settings.h"

#endif // HOME_APPS_H
