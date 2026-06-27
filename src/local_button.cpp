#include "local_button.h"

#include <Arduino.h>

#include "config.h"
#include "local_button_logic.h"
#include "mqtt_client.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

LongPressButtonState bootButtonState;
bool localButtonInitialized = false;
bool resetRequestedForCurrentPress = false;

bool bootButtonPressed() {
  return digitalRead(kBootButtonPin) == LOW;
}

void handleBootButtonEvent(LongPressButtonEvent event) {
  switch (event) {
    case LongPressButtonEvent::Pressed:
      resetRequestedForCurrentPress = false;
      Serial.printf("BOOT button pressed on %sGPIO%u%s; hold for %s%lu ms%s to reset credentials\n",
                    serialStyle(SerialStyle::Topic),
                    static_cast<unsigned int>(kBootButtonPin),
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    static_cast<unsigned long>(kBootButtonHoldMs),
                    serialReset());
      break;

    case LongPressButtonEvent::Released:
      Serial.printf("%sBOOT button released%s %s\n",
                    serialStyle(resetRequestedForCurrentPress ? SerialStyle::Success : SerialStyle::Warning),
                    serialReset(),
                    resetRequestedForCurrentPress ? "after reset request" : "before reset hold completed");
      resetRequestedForCurrentPress = false;
      break;

    case LongPressButtonEvent::Triggered:
      Serial.printf("%sBOOT button held%s for %s%lu ms%s; requesting credentials reset\n",
                    serialStyle(SerialStyle::Warning),
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    static_cast<unsigned long>(kBootButtonHoldMs),
                    serialReset());
      resetRequestedForCurrentPress = requestCredentialsReset("boot_button");
      if (resetRequestedForCurrentPress) {
        serviceCredentialResetRequests();
      }
      break;

    case LongPressButtonEvent::None:
      break;
  }
}

}  // namespace

void initializeLocalButton() {
  pinMode(kBootButtonPin, INPUT_PULLUP);
  localButtonInitialized = true;
  updateLongPressButton(bootButtonState,
                        bootButtonPressed(),
                        millis(),
                        kBootButtonDebounceMs,
                        kBootButtonHoldMs);
  Serial.printf("BOOT button credentials reset %s on %sGPIO%u%s active-low, hold %s%lu ms%s\n",
                serialEnabledDisabled(true),
                serialStyle(SerialStyle::Topic),
                static_cast<unsigned int>(kBootButtonPin),
                serialReset(),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned long>(kBootButtonHoldMs),
                serialReset());
}

void serviceLocalButton() {
  if (!localButtonInitialized) {
    return;
  }

  const LongPressButtonEvent event = updateLongPressButton(bootButtonState,
                                                           bootButtonPressed(),
                                                           millis(),
                                                           kBootButtonDebounceMs,
                                                           kBootButtonHoldMs);
  handleBootButtonEvent(event);
}

}  // namespace Esp32Meteo
