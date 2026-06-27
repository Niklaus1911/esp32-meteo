#include "local_button.h"

#include <Arduino.h>

#include "config.h"
#include "local_button_logic.h"
#include "mqtt_client.h"

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
      Serial.printf("BOOT button pressed on GPIO%u; hold for %lu ms to reset credentials\n",
                    static_cast<unsigned int>(kBootButtonPin),
                    static_cast<unsigned long>(kBootButtonHoldMs));
      break;

    case LongPressButtonEvent::Released:
      Serial.println(resetRequestedForCurrentPress ? "BOOT button released after reset request"
                                                   : "BOOT button released before reset hold completed");
      resetRequestedForCurrentPress = false;
      break;

    case LongPressButtonEvent::Triggered:
      Serial.printf("BOOT button held for %lu ms; requesting credentials reset\n",
                    static_cast<unsigned long>(kBootButtonHoldMs));
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
  Serial.printf("BOOT button credentials reset enabled on GPIO%u active-low, hold %lu ms\n",
                static_cast<unsigned int>(kBootButtonPin),
                static_cast<unsigned long>(kBootButtonHoldMs));
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
