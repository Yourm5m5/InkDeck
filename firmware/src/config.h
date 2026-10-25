// =============================================================================
//  config.h – pins and settings for InkDeck
// =============================================================================
//  Everything you might want to tweak is in this one file.
#pragma once
#include <stdint.h>

// ---- Grip PCB (cable to J3) -------------------------------------------------
//   J3-1 +3.3V   J3-2 GND
//   J3-3 BTN_MENU -> IO10  (10k pull-up on the PCB, wakes from deep sleep)
//   J3-4 BTN_PREV -> IO39  (internal pull-up)
//   J3-5 MOTOR    -> IO45  (AO3400 MOSFET, 10k pull-down on the PCB)
//   J3-6 BTN_NEXT -> IO48  (internal pull-up)
#define PIN_BTN_MENU 10      // RTC-capable pin -> the only button that can wake deep sleep
#define PIN_BTN_PREV 39
#define PIN_BTN_NEXT 48
#define PIN_MOTOR    45      // strapping pin! the PCB pull-down keeps it LOW at boot

// ---- On the LilyGo board ----------------------------------------------------
#define PIN_BAT_ADC  14      // battery voltage through a 1:2 divider
#define SD_MISO 16           // microSD on its own SPI bus
#define SD_MOSI 15
#define SD_SCK  11
#define SD_CS   42

// ---- Page layout (pixels, screen is 960 x 540) ------------------------------
const int MARGIN_X   = 40;
const int MARGIN_TOP = 30;
const int FOOTER_H   = 60;   // strip at the bottom for title / page / battery
const int INDENT_PX  = 36;   // first-line indent for paragraphs (a TAB in the text)

// ---- Buttons (milliseconds) -------------------------------------------------
const uint32_t DEBOUNCE_MS   = 25;    // contacts must be stable this long to count
const uint32_t HOLD_MS       = 700;   // PREV/NEXT held this long = chapter jump
const uint32_t MENU_LONG_MS  = 1000;  // MENU held 1 s  = Wi-Fi upload
const uint32_t MENU_VLONG_MS = 3000;  // MENU held 3 s  = sleep now

// ---- Power ------------------------------------------------------------------
const uint32_t SLEEP_AFTER_MS = 10UL * 60 * 1000;  // auto deep sleep after 10 min idle
const int      LOW_BATTERY    = 3;                 // % -> show "charge me" and sleep

// ---- Haptics ----------------------------------------------------------------
const bool HAPTICS_DEFAULT = true;    // can be switched off in the library ("Vibration: on/off" row)

// ---- Cache versions (bump to force every book to be re-processed) ----------
const uint32_t INDEX_VERSION = 0x494B0003;   // page index format / layout rules
const uint32_t META_VERSION  = 0x494D0001;   // converted-EPUB cache format

// ---- Wi-Fi upload hotspot ---------------------------------------------------
#define AP_NAME "InkDeck"
#define AP_PASS "readmore1"
