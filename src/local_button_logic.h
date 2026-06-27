#pragma once

#include <cstdint>

namespace Esp32Meteo {

enum class LongPressButtonEvent : uint8_t {
  None,
  Pressed,
  Released,
  Triggered,
};

struct LongPressButtonState {
  bool initialized = false;
  bool stablePressed = false;
  bool lastRawPressed = false;
  bool triggeredForPress = false;
  uint32_t lastRawChangeMs = 0;
  uint32_t pressedSinceMs = 0;
};

LongPressButtonEvent updateLongPressButton(LongPressButtonState& state,
                                           bool rawPressed,
                                           uint32_t nowMs,
                                           uint32_t debounceMs,
                                           uint32_t holdMs);

}  // namespace Esp32Meteo
