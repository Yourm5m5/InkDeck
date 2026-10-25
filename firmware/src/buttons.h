// =============================================================================
//  buttons.h – debounced buttons with click / hold / long-press detection
// =============================================================================
//  Wiring: every button connects its pin to GND when pressed ("active LOW").
//  A pull-up resistor holds the pin HIGH the rest of the time:
//      MENU (IO10)  -> 10k pull-up on the grip PCB (must still work in deep sleep)
//      PREV (IO39)  -> ESP32 internal pull-up
//      NEXT (IO48)  -> ESP32 internal pull-up
//
//  Debouncing: a mechanical switch "bounces" (flickers on/off) for a few ms
//  when pressed. We only accept a new state once the pin has stayed the same
//  for DEBOUNCE_MS, so one press is always exactly one event.
//
//  Two kinds of button:
//
//   PAGE buttons (PREV / NEXT)
//      quick press  -> CLICK  (sent when you let go)
//      hold 0.7 s   -> HOLD   (sent once, while still held, so the jump happens
//                              straight away instead of waiting for release)
//
//   MENU button – one button, three jobs, chosen by how long you hold it:
//      < 1 s        -> CLICK  (library / open book / leave upload mode)
//      1 – 3 s      -> LONG   (Wi-Fi upload mode)
//      > 3 s        -> VLONG  (sleep now)
//      While you hold it, the motor gives a CUE pulse at 1 s and again at 3 s,
//      so you can feel which action you'll get before letting go.
//      MENU acts on RELEASE because deep-sleep wake watches for MENU = LOW:
//      sleeping while it's still held down would wake the reader straight away.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "haptic.h"

enum BtnEvent { BTN_NONE, BTN_CLICK, BTN_HOLD, BTN_LONG, BTN_VLONG };
enum BtnKind  { PAGE_BUTTON, MENU_BUTTON };

class Button {
 public:
  Button(uint8_t pin, BtnKind kind) : pin_(pin), kind_(kind) {}

  // usePullup = true for PREV/NEXT (internal pull-up), false for MENU (external)
  void begin(bool usePullup) {
    pinMode(pin_, usePullup ? INPUT_PULLUP : INPUT);
    // If the button is already down at start-up (e.g. MENU just woke us from
    // sleep), ignore it until it's released so it doesn't count as a press.
    raw_ = stable_ = digitalRead(pin_);
    ignore_ = (stable_ == LOW);
  }

  bool isDown() const { return stable_ == LOW; }

  // Call every loop. Returns at most one event per call.
  BtnEvent update() {
    uint32_t now = millis();
    int r = digitalRead(pin_);
    if (r != raw_) { raw_ = r; changedAt_ = now; }          // still bouncing: restart timer

    BtnEvent ev = BTN_NONE;
    if (raw_ != stable_ && now - changedAt_ >= DEBOUNCE_MS) {
      stable_ = raw_;                                         // accepted a new state
      if (stable_ == LOW) {                                   // ---- pressed
        downAt_ = now; holdSent_ = false; cues_ = 0;
      } else {                                                // ---- released
        if (ignore_) { ignore_ = false; return BTN_NONE; }
        uint32_t held = now - downAt_;
        if (kind_ == PAGE_BUTTON) {
          if (!holdSent_) ev = BTN_CLICK;                     // HOLD already handled it
        } else {
          if      (held >= MENU_VLONG_MS) ev = BTN_VLONG;
          else if (held >= MENU_LONG_MS)  ev = BTN_LONG;
          else                            ev = BTN_CLICK;
        }
      }
    }

    // While held down: timed actions / cues
    if (stable_ == LOW && !ignore_) {
      uint32_t held = now - downAt_;
      if (kind_ == PAGE_BUTTON && !holdSent_ && held >= HOLD_MS) {
        holdSent_ = true;
        ev = BTN_HOLD;
      }
      if (kind_ == MENU_BUTTON) {
        if (cues_ == 0 && held >= MENU_LONG_MS)  { cues_ = 1; haptic::play(CUE); }
        if (cues_ == 1 && held >= MENU_VLONG_MS) { cues_ = 2; haptic::play(CUE); }
      }
    }
    return ev;
  }

 private:
  uint8_t  pin_;
  BtnKind  kind_;
  int      raw_ = HIGH, stable_ = HIGH;
  uint32_t changedAt_ = 0, downAt_ = 0;
  bool     holdSent_ = false, ignore_ = false;
  uint8_t  cues_ = 0;
};
