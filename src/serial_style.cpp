#include "serial_style.h"

namespace Esp32Meteo {

const char* serialStyleCode(SerialStyle style, bool enabled) {
  if (!enabled) {
    return "";
  }

  switch (style) {
    case SerialStyle::Phase:
      return "\033[1;36m";
    case SerialStyle::Success:
      return "\033[32m";
    case SerialStyle::Warning:
      return "\033[33m";
    case SerialStyle::Error:
      return "\033[31m";
    case SerialStyle::Muted:
      return "\033[2m";
    case SerialStyle::Highlight:
      return "\033[1m";
    case SerialStyle::Topic:
      return "\033[36m";
    case SerialStyle::Value:
      return "\033[35m";
    case SerialStyle::Plain:
    default:
      return "";
  }
}

const char* serialResetCode(bool enabled) {
  return enabled ? "\033[0m" : "";
}

SerialStyle serialResultStyle(bool ok) {
  return ok ? SerialStyle::Success : SerialStyle::Error;
}

SerialStyle serialReadyStyle(bool ready) {
  return ready ? SerialStyle::Success : SerialStyle::Error;
}

const char* serialOkFailedText(bool ok, bool enabled) {
  if (!enabled) {
    return ok ? "ok" : "FAILED";
  }
  return ok ? "\033[32mok\033[0m" : "\033[31mFAILED\033[0m";
}

const char* serialCompleteFailedText(bool ok, bool enabled) {
  if (!enabled) {
    return ok ? "complete" : "FAILED";
  }
  return ok ? "\033[32mcomplete\033[0m" : "\033[31mFAILED\033[0m";
}

const char* serialYesNoText(bool value, bool enabled) {
  if (!enabled) {
    return value ? "yes" : "no";
  }
  return value ? "\033[32myes\033[0m" : "\033[31mno\033[0m";
}

const char* serialTrueFalseText(bool value, bool enabled) {
  if (!enabled) {
    return value ? "true" : "false";
  }
  return value ? "\033[32mtrue\033[0m" : "\033[33mfalse\033[0m";
}

const char* serialEnabledDisabledText(bool value, bool enabled) {
  if (!enabled) {
    return value ? "enabled" : "disabled";
  }
  return value ? "\033[32menabled\033[0m" : "\033[33mdisabled\033[0m";
}

const char* serialPresentMissingText(bool present, bool enabled) {
  if (!enabled) {
    return present ? "present" : "MISSING";
  }
  return present ? "\033[32mpresent\033[0m" : "\033[31mMISSING\033[0m";
}

const char* serialReadyMissingText(bool ready, bool enabled) {
  if (!enabled) {
    return ready ? "ready" : "missing";
  }
  return ready ? "\033[32mready\033[0m" : "\033[31mmissing\033[0m";
}

}  // namespace Esp32Meteo
