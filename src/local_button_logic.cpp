#include "local_button_logic.h"

namespace Esp32Meteo {

LongPressButtonEvent updateLongPressButton(LongPressButtonState& state,
                                           bool rawPressed,
                                           uint32_t nowMs,
                                           uint32_t debounceMs,
                                           uint32_t holdMs) {
  if (!state.initialized) {
    state.initialized = true;
    state.stablePressed = rawPressed;
    state.lastRawPressed = rawPressed;
    state.lastRawChangeMs = nowMs;
    state.pressedSinceMs = rawPressed ? nowMs : 0;
    state.triggeredForPress = false;
    return LongPressButtonEvent::None;
  }

  if (rawPressed != state.lastRawPressed) {
    state.lastRawPressed = rawPressed;
    state.lastRawChangeMs = nowMs;
  }

  LongPressButtonEvent event = LongPressButtonEvent::None;
  if (rawPressed != state.stablePressed && nowMs - state.lastRawChangeMs >= debounceMs) {
    state.stablePressed = rawPressed;
    if (state.stablePressed) {
      state.pressedSinceMs = nowMs;
      state.triggeredForPress = false;
      event = LongPressButtonEvent::Pressed;
    } else {
      state.pressedSinceMs = 0;
      state.triggeredForPress = false;
      event = LongPressButtonEvent::Released;
    }
  }

  if (state.stablePressed && !state.triggeredForPress && nowMs - state.pressedSinceMs >= holdMs) {
    state.triggeredForPress = true;
    return LongPressButtonEvent::Triggered;
  }

  return event;
}

}  // namespace Esp32Meteo
