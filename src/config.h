#pragma once

#include <Arduino.h>
#include <WiFi.h>

#define ESP32_METEO_MODEL_ESP32_DEVKIT "ESP32 DevKit weather node"
#define ESP32_METEO_MODEL_ESP32C3_DEVKITM1 "ESP32-C3 DevKitM-1 weather node"
#define ESP32_METEO_TOPIC_PREFIX_ESP32 "esp32-meteo-v3"
#define ESP32_METEO_TOPIC_PREFIX_C3 "esp32-meteo-c3"
#define ESP32_METEO_HA_UNIQUE_PREFIX_ESP32 "esp32_meteo_v3"
#define ESP32_METEO_HA_UNIQUE_PREFIX_C3 "esp32_meteo_c3"
#define ESP32_METEO_HA_DEVICE_NAME_ESP32 "ESP32 Meteo V3"
#define ESP32_METEO_HA_DEVICE_NAME_C3 "ESP32 Meteo C3"

#ifdef ESP32_METEO_TARGET_C3
#define ESP32_METEO_TOPIC_PREFIX ESP32_METEO_TOPIC_PREFIX_C3
#define ESP32_METEO_STATUS_TOPIC ESP32_METEO_TOPIC_PREFIX_C3 "/status"
#define ESP32_METEO_STAY_AWAKE_TOPIC ESP32_METEO_TOPIC_PREFIX_C3 "/control/stay_awake"
#define ESP32_METEO_RESET_CREDENTIALS_TOPIC ESP32_METEO_TOPIC_PREFIX_C3 "/control/reset_credentials"
#define ESP32_METEO_HA_DEVICE_IDENTIFIER ESP32_METEO_TOPIC_PREFIX_C3
#define ESP32_METEO_HA_DEVICE_NAME ESP32_METEO_HA_DEVICE_NAME_C3
#define ESP32_METEO_HA_UNIQUE_PREFIX ESP32_METEO_HA_UNIQUE_PREFIX_C3
#define ESP32_METEO_OTA_HOSTNAME ESP32_METEO_TOPIC_PREFIX_C3
#define ESP32_METEO_DEFAULT_HA_MODEL ESP32_METEO_MODEL_ESP32C3_DEVKITM1
#else
#define ESP32_METEO_TOPIC_PREFIX ESP32_METEO_TOPIC_PREFIX_ESP32
#define ESP32_METEO_STATUS_TOPIC ESP32_METEO_TOPIC_PREFIX_ESP32 "/status"
#define ESP32_METEO_STAY_AWAKE_TOPIC ESP32_METEO_TOPIC_PREFIX_ESP32 "/control/stay_awake"
#define ESP32_METEO_RESET_CREDENTIALS_TOPIC ESP32_METEO_TOPIC_PREFIX_ESP32 "/control/reset_credentials"
#define ESP32_METEO_HA_DEVICE_IDENTIFIER ESP32_METEO_TOPIC_PREFIX_ESP32
#define ESP32_METEO_HA_DEVICE_NAME ESP32_METEO_HA_DEVICE_NAME_ESP32
#define ESP32_METEO_HA_UNIQUE_PREFIX ESP32_METEO_HA_UNIQUE_PREFIX_ESP32
#define ESP32_METEO_OTA_HOSTNAME ESP32_METEO_TOPIC_PREFIX_ESP32
#define ESP32_METEO_DEFAULT_HA_MODEL ESP32_METEO_MODEL_ESP32_DEVKIT
#endif

#ifndef ESP32_METEO_BOOT_BUTTON_PIN
#ifdef ESP32_METEO_TARGET_C3
#define ESP32_METEO_BOOT_BUTTON_PIN 9
#else
#define ESP32_METEO_BOOT_BUTTON_PIN 0
#endif
#endif

#ifndef ESP32_METEO_I2C_SDA_PIN
#define ESP32_METEO_I2C_SDA_PIN 21
#endif

#ifndef ESP32_METEO_I2C_SCL_PIN
#define ESP32_METEO_I2C_SCL_PIN 22
#endif

#ifndef ESP32_METEO_HA_MODEL
#define ESP32_METEO_HA_MODEL ESP32_METEO_DEFAULT_HA_MODEL
#endif

#ifndef ESP32_METEO_SERIAL_ANSI_COLORS
#define ESP32_METEO_SERIAL_ANSI_COLORS 1
#endif

#ifndef ESP32_METEO_WIFI_TX_POWER
#define ESP32_METEO_WIFI_TX_POWER WIFI_POWER_11dBm
#endif

namespace Esp32Meteo {

constexpr const char* wifiTxPowerLabel(wifi_power_t power) {
  switch (power) {
    case WIFI_POWER_21dBm:
      return "21 dBm";
    case WIFI_POWER_20_5dBm:
      return "20.5 dBm";
    case WIFI_POWER_20dBm:
      return "20 dBm";
    case WIFI_POWER_19_5dBm:
      return "19.5 dBm";
    case WIFI_POWER_19dBm:
      return "19 dBm";
    case WIFI_POWER_18_5dBm:
      return "18.5 dBm";
    case WIFI_POWER_17dBm:
      return "17 dBm";
    case WIFI_POWER_15dBm:
      return "15 dBm";
    case WIFI_POWER_13dBm:
      return "13 dBm";
    case WIFI_POWER_11dBm:
      return "11 dBm";
    case WIFI_POWER_8_5dBm:
      return "8.5 dBm";
    case WIFI_POWER_7dBm:
      return "7 dBm";
    case WIFI_POWER_5dBm:
      return "5 dBm";
    case WIFI_POWER_2dBm:
      return "2 dBm";
    case WIFI_POWER_MINUS_1dBm:
      return "-1 dBm";
    default:
      return "custom";
  }
}

constexpr uint32_t kSerialBaud = 115200;
constexpr bool kSerialAnsiColors = ESP32_METEO_SERIAL_ANSI_COLORS != 0;
constexpr uint8_t kBootButtonPin = static_cast<uint8_t>(ESP32_METEO_BOOT_BUTTON_PIN);
constexpr uint8_t kI2cSdaPin = static_cast<uint8_t>(ESP32_METEO_I2C_SDA_PIN);
constexpr uint8_t kI2cSclPin = static_cast<uint8_t>(ESP32_METEO_I2C_SCL_PIN);

constexpr uint8_t kSolarInaAddress = 0x40;
constexpr uint8_t kBatteryInaAddress = 0x41;
constexpr uint8_t kSht41Address = 0x44;
constexpr uint8_t kBmp390Address = 0x77;

constexpr const char* kFirmwareName = ESP32_METEO_TOPIC_PREFIX;
constexpr const char* kTopicPrefix = ESP32_METEO_TOPIC_PREFIX;
constexpr const char* kStayAwakeTopic = ESP32_METEO_STAY_AWAKE_TOPIC;
constexpr const char* kResetCredentialsTopic = ESP32_METEO_RESET_CREDENTIALS_TOPIC;
constexpr const char* kStatusTopic = ESP32_METEO_STATUS_TOPIC;
constexpr const char* kHaDiscoveryPrefix = "homeassistant";
constexpr const char* kHaStatusTopic = "homeassistant/status";
constexpr const char* kHaDeviceIdentifier = ESP32_METEO_HA_DEVICE_IDENTIFIER;
constexpr const char* kHaDeviceName = ESP32_METEO_HA_DEVICE_NAME;
constexpr const char* kHaManufacturer = "DIY";
constexpr const char* kHaModel = ESP32_METEO_HA_MODEL;
constexpr const char* kHaUniqueIdPrefix = ESP32_METEO_HA_UNIQUE_PREFIX;
constexpr const char* kOtaHostname = ESP32_METEO_OTA_HOSTNAME;

constexpr uint64_t kDeepSleepSeconds = 10ULL * 60ULL;
constexpr uint32_t kStayAwakePublishIntervalMs = 10UL * 1000UL;
constexpr uint32_t kWifiConnectTimeoutMs = 20UL * 1000UL;
constexpr uint32_t kMqttConnectTimeoutMs = 10UL * 1000UL;
constexpr uint16_t kMqttSocketTimeoutSeconds = 3;
constexpr uint32_t kMqttNetworkTimeoutMs = 3000;
constexpr uint32_t kMqttPostOnlineSetupBudgetMs = 20UL * 1000UL;
constexpr uint32_t kHaDiscoveryBudgetMs = 12UL * 1000UL;
constexpr uint32_t kRetainedCommandWaitMs = 5000;
constexpr uint32_t kTelemetryFlushMs = 750;
constexpr uint32_t kTelemetryPublishRetryDelayMs = 1000;
constexpr uint32_t kSensorGroupPublishGapMs = 1000;
constexpr uint32_t kPostTelemetryAwakeMs = 10UL * 1000UL;
constexpr uint32_t kBootButtonHoldMs = 4000;
constexpr uint32_t kBootButtonDebounceMs = 50;
constexpr uint32_t kSleepStatusConfirmTimeoutMs = 3000;
constexpr uint8_t kSleepStatusConfirmAttempts = 3;
constexpr uint32_t kPostSleepStatusGraceMs = 3000;
constexpr uint32_t kWifiRetryDelayMs = 250;
constexpr uint32_t kMqttRetryDelayMs = 500;
constexpr uint32_t kDeepSleepWakeStabilizeMs = 2500;
constexpr uint32_t kPreWifiStartStabilizeMs = 1000;
constexpr uint32_t kI2cPowerStabilizeDelayMs = 1000;
constexpr uint32_t kSensorPostInitSettleDelayMs = 1000;
constexpr uint32_t kBmp390InitRetryDelayMs = 1000;
constexpr uint32_t kBmp390WarmupDiscardDelayMs = 250;
constexpr uint32_t kBeforeFirstReadDelayMs = 500;
constexpr uint8_t kBmp390InitAttempts = 3;
constexpr uint8_t kI2cRecoveryClockPulses = 9;
constexpr uint32_t kI2cRecoveryPulseDelayUs = 5;
constexpr uint32_t kCpuFrequencyMhz = 80;
constexpr wifi_power_t kWifiTxPower = ESP32_METEO_WIFI_TX_POWER;
constexpr const char* kWifiTxPowerLabel = wifiTxPowerLabel(kWifiTxPower);
constexpr size_t kMqttBufferSize = 2048;
constexpr size_t kMqttMaxHeaderBytes = 5;

constexpr float kInaShuntOhms = 0.1f;
constexpr float kSolarMaxCurrentA = 1.0f;
constexpr float kBatteryMaxCurrentA = 0.8f;

// Set true during local serial/MQTT testing to keep the device awake without a retained MQTT command.
// Keep false for production so the node returns to deep sleep unless MQTT explicitly requests stay-awake.
constexpr bool kForceStayAwakeForTesting = false;

}  // namespace Esp32Meteo
