#pragma once
#include "epd_driver.h"
static UnicodeInterval FiraSans_Intervals[] = {{32, 126, 0}, {160, 255, 0}, {0x2010, 0x205F, 0}};
static const GFXfont FiraSans = {nullptr, nullptr, FiraSans_Intervals, 3, false, 50, 39, -12};
