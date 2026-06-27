#include "ota_service.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "config.h"
#include "mqtt_client.h"
#include "runtime_config.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

void handleOta() {
  ArduinoOTA.handle();
}

void initializeOta() {
  logPhase("OTA");
  const RuntimeConfig& config = runtimeConfig();
  if (!config.otaPassword[0]) {
    Serial.printf("%sOTA disabled%s: runtime OTA password is missing\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    return;
  }

  ArduinoOTA.setHostname(kOtaHostname);
  ArduinoOTA.setPassword(config.otaPassword);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    Serial.printf("%sOTA update started%s; deep sleep is blocked until update finishes\n",
                  serialStyle(SerialStyle::Highlight),
                  serialReset());
    if (mqttClient().connected()) {
      mqttClient().publish(kStatusTopic, "ota_updating", true);
    }
  });

  ArduinoOTA.onEnd([]() {
    Serial.println();
    Serial.printf("%sOTA update finished%s; ESP32 will reboot\n",
                  serialStyle(SerialStyle::Success),
                  serialReset());
    otaInProgress = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    static uint8_t lastPercent = 255;
    const uint8_t percent = static_cast<uint8_t>((progress * 100U) / total);
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("OTA progress: %s%u%%%s\n", serialStyle(SerialStyle::Value), percent, serialReset());
      lastPercent = percent;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("%sOTA error %u%s: ", serialStyle(SerialStyle::Error), error, serialReset());
    if (error == OTA_AUTH_ERROR) {
      Serial.println("auth failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("begin failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("connect failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("receive failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("end failed");
    } else {
      Serial.println("unknown");
    }
  });

  ArduinoOTA.begin();
  Serial.printf("%sArduinoOTA ready%s, hostname %s%s%s, IP %s%s%s\n",
                serialStyle(SerialStyle::Success),
                serialReset(),
                serialStyle(SerialStyle::Topic),
                kOtaHostname,
                serialReset(),
                serialStyle(SerialStyle::Value),
                WiFi.localIP().toString().c_str(),
                serialReset());
  Serial.println("OTA password loaded from runtime config and will not be printed");
}

}  // namespace Esp32Meteo
