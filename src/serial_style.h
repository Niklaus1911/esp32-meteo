#pragma once

#include <cstdint>

namespace Esp32Meteo {

enum class SerialStyle : uint8_t {
  Plain,
  Phase,
  Success,
  Warning,
  Error,
  Muted,
  Highlight,
  Topic,
  Value,
};

const char* serialStyleCode(SerialStyle style, bool enabled);
const char* serialResetCode(bool enabled);
SerialStyle serialResultStyle(bool ok);
SerialStyle serialReadyStyle(bool ready);
const char* serialOkFailedText(bool ok, bool enabled);
const char* serialCompleteFailedText(bool ok, bool enabled);
const char* serialYesNoText(bool value, bool enabled);
const char* serialTrueFalseText(bool value, bool enabled);
const char* serialEnabledDisabledText(bool value, bool enabled);
const char* serialPresentMissingText(bool present, bool enabled);
const char* serialReadyMissingText(bool ready, bool enabled);

}  // namespace Esp32Meteo
