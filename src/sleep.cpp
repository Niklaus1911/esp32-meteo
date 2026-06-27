#include "sleep.h"

#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "mqtt_client.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void releaseI2cPinsBeforeSleep() {
  Wire.end();
  pinMode(kI2cSdaPin, INPUT);
  pinMode(kI2cSclPin, INPUT);
  Serial.printf("I2C stopped and pins released before deep sleep: SDA %sGPIO%u%s, SCL %sGPIO%u%s\n",
                serialStyle(SerialStyle::Topic),
                static_cast<unsigned int>(kI2cSdaPin),
                serialReset(),
                serialStyle(SerialStyle::Topic),
                static_cast<unsigned int>(kI2cSclPin),
                serialReset());
}

void waitForPostTelemetryAwakeWindow() {
  if (!telemetryPublishCompleted) {
    Serial.printf("%sTelemetry publish was not completed%s; skipping post-telemetry awake wait\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    return;
  }

  const uint32_t elapsedMs = millis() - telemetryPublishCompletedMs;
  if (elapsedMs >= kPostTelemetryAwakeMs) {
    Serial.printf("Post-telemetry awake window already satisfied: %lu/%lu ms\n",
                  static_cast<unsigned long>(elapsedMs),
                  static_cast<unsigned long>(kPostTelemetryAwakeMs));
    return;
  }

  const uint32_t remainingMs = kPostTelemetryAwakeMs - elapsedMs;
  Serial.printf("Waiting %lu ms to complete post-telemetry awake window (%lu/%lu ms elapsed)\n",
                static_cast<unsigned long>(remainingMs),
                static_cast<unsigned long>(elapsedMs),
                static_cast<unsigned long>(kPostTelemetryAwakeMs));
  waitWithMqttAndOta(remainingMs, "Post-telemetry awake window wait");
}

}  // namespace

void sleepForDefaultInterval(const char* reason) {
  logPhase("Deep sleep");
  while (otaInProgress) {
    Serial.printf("%sOTA update in progress%s; delaying deep sleep\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    serviceMqttAndOta();
    delay(100);
  }

  publishBootPhase("sleep_prepare");
  Serial.printf("Preparing deep sleep for %s%llu seconds%s: %s%s%s\n",
                serialStyle(SerialStyle::Value),
                kDeepSleepSeconds,
                serialReset(),
                serialStyle(SerialStyle::Warning),
                reason,
                serialReset());
  waitForPostTelemetryAwakeWindow();

  if (mqttClient().connected()) {
    const bool sleepStatusConfirmed = publishSleepStatusWithConfirmation();
    if (sleepStatusConfirmed) {
      waitWithMqttAndOta(kPostSleepStatusGraceMs, "Post-sleep-status MQTT grace period");
    }
    mqttClient().disconnect();
    Serial.printf("%sMQTT disconnected%s\n", serialStyle(SerialStyle::Muted), serialReset());
  } else {
    Serial.printf("%sMQTT not connected%s; skipping sleeping status publish\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.printf("%sWiFi disconnected%s and radio turned off\n", serialStyle(SerialStyle::Muted), serialReset());
  releaseI2cPinsBeforeSleep();
  ++rtcSleepEntryCount;
  Serial.printf("RTC sleep entry counter: %lu\n", static_cast<unsigned long>(rtcSleepEntryCount));
  esp_sleep_enable_timer_wakeup(kDeepSleepSeconds * 1000000ULL);
  Serial.printf("%sDeep-sleep timer configured%s\n", serialStyle(SerialStyle::Success), serialReset());
  Serial.printf("Entering deep sleep for %s%llu seconds%s now\n",
                serialStyle(SerialStyle::Value),
                kDeepSleepSeconds,
                serialReset());
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace Esp32Meteo
