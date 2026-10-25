// =============================================================================
//  haptic.h – vibration patterns for the coin motor
// =============================================================================
//  The motor is switched by an AO3400 MOSFET on IO45:
//      IO45 HIGH -> MOSFET on -> motor spins
//      IO45 LOW  -> MOSFET off -> motor stops
//  A flyback diode on the grip PCB protects the MOSFET when the motor stops.
//
//  Each action gets its own "feel", so you know what happened without looking:
//      TICK    one short pulse         page turn, move in the library
//      DOUBLE  two pulses              jumped a whole chapter
//      CUE     one medium pulse        a MENU hold reached the next level
//      ERROR   three quick pulses      can't go further (first / last page)
//      SLEEP   one long pulse          going to sleep
//
//  Patterns block for at most ~150 ms. That's fine: the e-paper refresh that
//  follows takes far longer, and keeping the motor off during the refresh
//  avoids a dip in the supply while the display is drawing.
#pragma once
#include <Arduino.h>
#include "config.h"

enum HapticPattern { TICK, DOUBLE, CUE, ERROR_BUZZ, SLEEP_BUZZ };

namespace haptic {

inline bool& enabled() { static bool on = HAPTICS_DEFAULT; return on; }

inline void begin() {
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);          // off straight away at boot
}

inline void pulse(int onMs) {
  digitalWrite(PIN_MOTOR, HIGH);
  delay(onMs);
  digitalWrite(PIN_MOTOR, LOW);
}

inline void play(HapticPattern p) {
  if (!enabled()) return;
  switch (p) {
    case TICK:       pulse(20); break;
    case DOUBLE:     pulse(20); delay(70); pulse(20); break;
    case CUE:        pulse(45); break;
    case ERROR_BUZZ: for (int i = 0; i < 3; i++) { pulse(15); delay(40); } break;
    case SLEEP_BUZZ: pulse(120); break;
  }
}

}  // namespace haptic
