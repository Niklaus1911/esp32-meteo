#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_BMP3XX.h>
#include <Adafruit_SHT4x.h>
#include <INA226_WE.h>
#include <esp_sleep.h>
#include <cstdarg>

#include "secrets_local.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;

constexpr uint8_t kSolarInaAddress = 0x40;
constexpr uint8_t kBatteryInaAddress = 0x41;
constexpr uint8_t kSht41Address = 0x44;
constexpr uint8_t kBmp390Address = 0x77;

constexpr const char* kFirmwareName = "esp32-meteo-v3";
constexpr const char* kTopicPrefix = "esp32-meteo-v3";
constexpr const char* kStayAwakeTopic = "esp32-meteo-v3/control/stay_awake";
constexpr const char* kStatusTopic = "esp32-meteo-v3/status";
constexpr const char* kHaDiscoveryPrefix = "homeassistant";
constexpr const char* kHaStatusTopic = "homeassistant/status";
constexpr const char* kHaDeviceIdentifier = "esp32-meteo-v3";
constexpr const char* kHaDeviceName = "ESP32 Meteo V3";
constexpr const char* kHaManufacturer = "DIY";
constexpr const char* kHaModel = "ESP32 DevKit weather node";

constexpr uint64_t kDeepSleepSeconds = 10ULL * 60ULL;
constexpr uint32_t kStayAwakePublishIntervalMs = 10UL * 1000UL;
constexpr uint32_t kWifiConnectTimeoutMs = 20UL * 1000UL;
constexpr uint32_t kMqttConnectTimeoutMs = 10UL * 1000UL;
constexpr uint32_t kRetainedCommandWaitMs = 5000;
constexpr uint32_t kMqttFlushMs = 1500;
constexpr uint32_t kTelemetryFlushMs = 750;
constexpr uint32_t kWifiRetryDelayMs = 250;
constexpr uint32_t kMqttRetryDelayMs = 500;
constexpr uint32_t kI2cPowerStabilizeDelayMs = 1000;
constexpr uint32_t kSensorPostInitSettleDelayMs = 1000;
constexpr uint32_t kBmp390InitRetryDelayMs = 1000;
constexpr uint32_t kBmp390WarmupDiscardDelayMs = 250;
constexpr uint32_t kBeforeFirstReadDelayMs = 500;
constexpr uint8_t kBmp390InitAttempts = 3;
constexpr size_t kMqttBufferSize = 2048;
constexpr size_t kMqttMaxHeaderBytes = 5;

constexpr float kInaShuntOhms = 0.1f;
constexpr float kSolarMaxCurrentA = 1.0f;
constexpr float kBatteryMaxCurrentA = 0.8f;

// Set true during local serial/MQTT testing to keep the device awake without a retained MQTT command.
// Keep false for production so the node returns to deep sleep unless MQTT explicitly requests stay-awake.
constexpr bool kForceStayAwakeForTesting = false;

struct BatteryCurvePoint {
  float voltage;
  float percent;
};

constexpr BatteryCurvePoint kBatteryCurve[] = {
    {3.20f, 0.0f},   {3.40f, 10.0f},  {3.58f, 20.0f},
    {3.68f, 30.0f},  {3.74f, 40.0f},  {3.79f, 50.0f},
    {3.85f, 60.0f},  {3.92f, 70.0f},  {4.00f, 80.0f},
    {4.10f, 90.0f},  {4.16f, 100.0f},
};

struct DeviceState {
  bool solarInaPresent = false;
  bool batteryInaPresent = false;
  bool sht41Present = false;
  bool bmp390Present = false;
  bool solarInaReady = false;
  bool batteryInaReady = false;
  bool sht41Ready = false;
  bool bmp390Ready = false;
};

struct Reading {
  float bmpTemperatureC = NAN;
  float absolutePressureHpa = NAN;
  float outsideTemperatureC = NAN;
  float outsideHumidityPercent = NAN;
  float batteryVoltageV = NAN;
  float batteryCurrentMa = NAN;
  float batteryPowerW = NAN;
  float batteryLevelPercent = NAN;
  float solarRawVoltageV = NAN;
  float solarPanelCurrentMa = NAN;
  float solarRawPowerW = NAN;
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_BMP3XX bmp;
Adafruit_SHT4x sht4;
INA226_WE solarIna(kSolarInaAddress);
INA226_WE batteryIna(kBatteryInaAddress);
DeviceState devices;

bool stayAwakeRequested = false;
bool stayAwakeCommandReceived = false;
bool homeAssistantDiscoveryRequested = false;
bool otaInProgress = false;
uint32_t lastPublishMs = 0;

void publishHomeAssistantDiscovery();
void flushMqtt(uint32_t durationMs);
void waitForRetainedStayAwakeCommand();

void logPhase(const char* phase) {
  Serial.println();
  Serial.printf("== %s ==\n", phase);
}

const char* yesNo(bool value) {
  return value ? "yes" : "no";
}

bool formatInto(char* buffer, size_t bufferSize, const char* description, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, bufferSize, format, args);
  va_end(args);

  if (written < 0) {
    Serial.printf("Formatting failed for %s\n", description);
    return false;
  }
  if (static_cast<size_t>(written) >= bufferSize) {
    Serial.printf("Formatting truncated for %s: needed %u bytes, buffer has %u bytes\n",
                  description,
                  static_cast<unsigned int>(written + 1),
                  static_cast<unsigned int>(bufferSize));
    return false;
  }
  return true;
}

bool appendChecked(char* destination, size_t destinationSize, const char* description, const char* value) {
  const size_t used = strlen(destination);
  const size_t valueLength = strlen(value);
  if (used + valueLength >= destinationSize) {
    Serial.printf("Formatting truncated for %s: needed %u bytes, buffer has %u bytes\n",
                  description,
                  static_cast<unsigned int>(used + valueLength + 1),
                  static_cast<unsigned int>(destinationSize));
    return false;
  }

  strlcat(destination, value, destinationSize);
  return true;
}

String topic(const char* suffix) {
  String full(kTopicPrefix);
  full += suffix;
  return full;
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "unknown";
  }
}

bool i2cAddressPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void logExpectedDevice(uint8_t address, const char* name, bool present) {
  Serial.printf("I2C 0x%02X %-18s %s\n", address, name, present ? "present" : "MISSING");
}

void scanI2cBus() {
  logPhase("I2C scan");
  Serial.printf("I2C pins: SDA GPIO%u, SCL GPIO%u\n", kI2cSdaPin, kI2cSclPin);
  Serial.println("Expected devices:");
  Serial.printf("  0x%02X INA226 solar monitor\n", kSolarInaAddress);
  Serial.printf("  0x%02X INA226 battery monitor\n", kBatteryInaAddress);
  Serial.printf("  0x%02X SHT41/SHT4x sensor\n", kSht41Address);
  Serial.printf("  0x%02X BMP390/BMP3xx sensor\n", kBmp390Address);
  Serial.println("Scanning I2C bus now");

  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cAddressPresent(address)) {
      Serial.printf("  found 0x%02X\n", address);
      ++found;
    }
  }
  if (found == 0) {
    Serial.println("  no I2C devices detected");
  }

  devices.solarInaPresent = i2cAddressPresent(kSolarInaAddress);
  devices.batteryInaPresent = i2cAddressPresent(kBatteryInaAddress);
  devices.sht41Present = i2cAddressPresent(kSht41Address);
  devices.bmp390Present = i2cAddressPresent(kBmp390Address);

  logExpectedDevice(kSolarInaAddress, "INA226 solar", devices.solarInaPresent);
  logExpectedDevice(kBatteryInaAddress, "INA226 battery", devices.batteryInaPresent);
  logExpectedDevice(kSht41Address, "SHT41/SHT4x", devices.sht41Present);
  logExpectedDevice(kBmp390Address, "BMP390/BMP3xx", devices.bmp390Present);
  Serial.printf(
      "I2C expected-device summary: solar=%s battery=%s sht4x=%s bmp3xx=%s\n",
      yesNo(devices.solarInaPresent),
      yesNo(devices.batteryInaPresent),
      yesNo(devices.sht41Present),
      yesNo(devices.bmp390Present));
}

void configureBmp390AfterInit() {
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_16X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_32X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_15);
  Serial.println("BMP390/BMP3xx initialized with pressure 32x, temperature 16x, IIR 16x");
  // The BMP3XX first sample after startup may be inaccurate; discard it before publishing.
  if (bmp.performReading()) {
    Serial.printf("BMP390/BMP3xx warm-up discard: temperature %.2f C, pressure %.2f hPa\n",
                  bmp.temperature,
                  bmp.pressure / 100.0f);
  } else {
    Serial.println("BMP390/BMP3xx warm-up discard read failed");
  }
  Serial.printf("Waiting %lu ms after BMP390/BMP3xx warm-up discard\n",
                static_cast<unsigned long>(kBmp390WarmupDiscardDelayMs));
  delay(kBmp390WarmupDiscardDelayMs);
}

void initializeBmp390() {
  devices.bmp390Ready = false;

  for (uint8_t attempt = 1; attempt <= kBmp390InitAttempts; ++attempt) {
    devices.bmp390Present = i2cAddressPresent(kBmp390Address);
    if (!devices.bmp390Present) {
      Serial.printf("BMP390/BMP3xx not present at 0x%02X on init attempt %u/%u\n",
                    kBmp390Address,
                    attempt,
                    kBmp390InitAttempts);
    } else {
      Serial.printf("Initializing BMP390/BMP3xx at 0x%02X, attempt %u/%u\n",
                    kBmp390Address,
                    attempt,
                    kBmp390InitAttempts);
      devices.bmp390Ready = bmp.begin_I2C(kBmp390Address, &Wire);
      if (devices.bmp390Ready) {
        configureBmp390AfterInit();
        return;
      }
      Serial.printf("BMP390/BMP3xx initialization failed on attempt %u/%u\n", attempt, kBmp390InitAttempts);
    }

    if (attempt < kBmp390InitAttempts) {
      Serial.printf("Waiting %lu ms before BMP390/BMP3xx init retry\n",
                    static_cast<unsigned long>(kBmp390InitRetryDelayMs));
      delay(kBmp390InitRetryDelayMs);
    }
  }

  if (!devices.bmp390Present) {
    Serial.printf("Skipping BMP390/BMP3xx init because 0x%02X is missing after retries\n", kBmp390Address);
  } else {
    Serial.println("BMP390/BMP3xx initialization failed after retries");
  }
}

void initializeSensors() {
  logPhase("Sensor initialization");
  initializeBmp390();

  if (devices.sht41Present) {
    Serial.printf("Initializing SHT41/SHT4x at 0x%02X\n", kSht41Address);
    devices.sht41Ready = sht4.begin(&Wire);
    if (devices.sht41Ready) {
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sht4.setHeater(SHT4X_NO_HEATER);
      Serial.println("SHT41/SHT4x initialized with high precision and heater off");
    } else {
      Serial.println("SHT41/SHT4x initialization failed");
    }
  } else {
    Serial.printf("Skipping SHT41/SHT4x init because 0x%02X is missing\n", kSht41Address);
  }

  if (devices.solarInaPresent) {
    Serial.printf("Initializing solar INA226 at 0x%02X, shunt %.3f ohm, max current %.2f A\n",
                  kSolarInaAddress,
                  kInaShuntOhms,
                  kSolarMaxCurrentA);
    devices.solarInaReady = solarIna.init();
    if (devices.solarInaReady) {
      solarIna.setResistorRange(kInaShuntOhms, kSolarMaxCurrentA);
      solarIna.waitUntilConversionCompleted();
      Serial.println("Solar INA226 initialized");
    } else {
      Serial.println("Solar INA226 initialization failed");
    }
  } else {
    Serial.printf("Skipping solar INA226 init because 0x%02X is missing\n", kSolarInaAddress);
  }

  if (devices.batteryInaPresent) {
    Serial.printf("Initializing battery INA226 at 0x%02X, shunt %.3f ohm, max current %.2f A\n",
                  kBatteryInaAddress,
                  kInaShuntOhms,
                  kBatteryMaxCurrentA);
    devices.batteryInaReady = batteryIna.init();
    if (devices.batteryInaReady) {
      batteryIna.setResistorRange(kInaShuntOhms, kBatteryMaxCurrentA);
      batteryIna.waitUntilConversionCompleted();
      Serial.println("Battery INA226 initialized");
    } else {
      Serial.println("Battery INA226 initialization failed");
    }
  } else {
    Serial.printf("Skipping battery INA226 init because 0x%02X is missing\n", kBatteryInaAddress);
  }

  Serial.printf(
      "Sensor readiness summary: solar=%s battery=%s sht4x=%s bmp3xx=%s\n",
      yesNo(devices.solarInaReady),
      yesNo(devices.batteryInaReady),
      yesNo(devices.sht41Ready),
      yesNo(devices.bmp390Ready));

  Serial.printf("Waiting %lu ms for sensors to settle after initialization\n",
                static_cast<unsigned long>(kSensorPostInitSettleDelayMs));
  delay(kSensorPostInitSettleDelayMs);
}

float batteryLevelPercent(float voltage) {
  if (isnan(voltage)) {
    return NAN;
  }
  if (voltage <= kBatteryCurve[0].voltage) {
    return 0.0f;
  }

  constexpr size_t pointCount = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);
  if (voltage >= kBatteryCurve[pointCount - 1].voltage) {
    return 100.0f;
  }

  for (size_t i = 1; i < pointCount; ++i) {
    const BatteryCurvePoint& lower = kBatteryCurve[i - 1];
    const BatteryCurvePoint& upper = kBatteryCurve[i];
    if (voltage <= upper.voltage) {
      const float ratio = (voltage - lower.voltage) / (upper.voltage - lower.voltage);
      return lower.percent + ratio * (upper.percent - lower.percent);
    }
  }
  return 100.0f;
}

Reading readSensors() {
  logPhase("Sensor read");
  Reading reading;

  if (devices.bmp390Ready) {
    if (bmp.performReading()) {
      reading.bmpTemperatureC = bmp.temperature;
      reading.absolutePressureHpa = bmp.pressure / 100.0f;
      Serial.printf("BMP390/BMP3xx: temperature %.2f C, pressure %.2f hPa\n",
                    reading.bmpTemperatureC,
                    reading.absolutePressureHpa);
    } else {
      Serial.println("BMP390/BMP3xx read failed");
    }
  } else {
    Serial.println("BMP390/BMP3xx skipped: not ready");
  }

  if (devices.sht41Ready) {
    sensors_event_t humidity;
    sensors_event_t temperature;
    if (sht4.getEvent(&humidity, &temperature)) {
      reading.outsideTemperatureC = temperature.temperature;
      reading.outsideHumidityPercent = humidity.relative_humidity;
      Serial.printf("SHT41/SHT4x: temperature %.2f C, humidity %.2f %%\n",
                    reading.outsideTemperatureC,
                    reading.outsideHumidityPercent);
    } else {
      Serial.println("SHT41/SHT4x read failed");
    }
  } else {
    Serial.println("SHT41/SHT4x skipped: not ready");
  }

  if (devices.batteryInaReady) {
    // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
    reading.batteryVoltageV = batteryIna.getBusVoltage_V();
    reading.batteryCurrentMa = batteryIna.getCurrent_mA();
    reading.batteryPowerW = batteryIna.getBusPower() / 1000.0f;
    reading.batteryLevelPercent = batteryLevelPercent(reading.batteryVoltageV);
    Serial.printf("Battery INA226: voltage %.3f V, current %.2f mA, power %.3f W, level %.1f %%\n",
                  reading.batteryVoltageV,
                  reading.batteryCurrentMa,
                  reading.batteryPowerW,
                  reading.batteryLevelPercent);
  } else {
    Serial.println("Battery INA226 skipped: not ready");
  }

  if (devices.solarInaReady) {
    // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
    reading.solarRawVoltageV = solarIna.getBusVoltage_V();
    reading.solarPanelCurrentMa = solarIna.getCurrent_mA();
    reading.solarRawPowerW = solarIna.getBusPower() / 1000.0f;
    Serial.printf("Solar INA226: voltage %.3f V, current %.2f mA, power %.3f W\n",
                  reading.solarRawVoltageV,
                  reading.solarPanelCurrentMa,
                  reading.solarRawPowerW);
  } else {
    Serial.println("Solar INA226 skipped: not ready");
  }

  return reading;
}

bool configureStaticWifiIp() {
  if (!WIFI_HAS_STATIC_IP) {
    Serial.println("WiFi static IP not configured; using DHCP");
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;

  if (!localIp.fromString(WIFI_STATIC_IP) || !gateway.fromString(WIFI_GATEWAY) || !subnet.fromString(WIFI_SUBNET)) {
    Serial.println("WiFi static IP constants are invalid; falling back to DHCP");
    return false;
  }

  const bool configured = WiFi.config(localIp, gateway, subnet);
  Serial.printf("WiFi static IP config %s, IP %s, gateway %s, subnet %s\n",
                configured ? "applied" : "FAILED",
                localIp.toString().c_str(),
                gateway.toString().c_str(),
                subnet.toString().c_str());
  return configured;
}

bool connectWifiSlot(const char* slotName, const char* ssid, const char* password) {
  Serial.printf("Connecting to WiFi %s slot with timeout %lu ms\n",
                slotName,
                static_cast<unsigned long>(kWifiConnectTimeoutMs));
  Serial.println("WiFi password will not be printed");

  WiFi.begin(ssid, password);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiConnectTimeoutMs) {
    delay(kWifiRetryDelayMs);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi %s slot failed after %lu ms, status=%d\n",
                  slotName,
                  static_cast<unsigned long>(millis() - started),
                  WiFi.status());
    WiFi.disconnect(false);
    delay(250);
    return false;
  }

  Serial.printf("WiFi %s slot connected in %lu ms, RSSI %d dBm, IP %s\n",
                slotName,
                static_cast<unsigned long>(millis() - started),
                WiFi.RSSI(),
                WiFi.localIP().toString().c_str());
  return true;
}

bool connectWifi() {
  logPhase("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  configureStaticWifiIp();

  Serial.println("WiFi credentials loaded from generated header");
  if (connectWifiSlot("primary", WIFI_PRIMARY_SSID, WIFI_PRIMARY_PASSWORD)) {
    return true;
  }

  if (WIFI_HAS_BACKUP) {
    Serial.println("Primary WiFi failed; trying backup WiFi slot");
    return connectWifiSlot("backup", WIFI_BACKUP_SSID, WIFI_BACKUP_PASSWORD);
  }

  Serial.println("Primary WiFi failed and no backup WiFi slot is configured");
  return false;
}

void initializeOta() {
  logPhase("OTA");
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    Serial.println("OTA update started; deep sleep is blocked until update finishes");
    if (mqttClient.connected()) {
      mqttClient.publish(kStatusTopic, "ota_updating", true);
    }
  });

  ArduinoOTA.onEnd([]() {
    Serial.println();
    Serial.println("OTA update finished; ESP32 will reboot");
    otaInProgress = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    static uint8_t lastPercent = 255;
    const uint8_t percent = static_cast<uint8_t>((progress * 100U) / total);
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("OTA progress: %u%%\n", percent);
      lastPercent = percent;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("OTA error %u: ", error);
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
  Serial.printf("ArduinoOTA ready, hostname %s, IP %s\n", OTA_HOSTNAME, WiFi.localIP().toString().c_str());
  Serial.println("OTA password loaded from generated header and will not be printed");
}

bool parseStayAwakePayload(const char* payload, size_t length, bool& value) {
  String normalized;
  normalized.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    if (!isspace(static_cast<unsigned char>(payload[i]))) {
      normalized += static_cast<char>(tolower(static_cast<unsigned char>(payload[i])));
    }
  }

  if (normalized == "true" || normalized == "on" || normalized == "1" || normalized == "yes") {
    value = true;
    return true;
  }
  if (normalized == "false" || normalized == "off" || normalized == "0" || normalized == "no") {
    value = false;
    return true;
  }
  return false;
}

void mqttCallback(char* receivedTopic, byte* payload, unsigned int length) {
  if (strcmp(receivedTopic, kHaStatusTopic) == 0) {
    bool isHomeAssistantOnline = length == 6;
    const char expected[] = "online";
    for (unsigned int i = 0; isHomeAssistantOnline && i < length; ++i) {
      isHomeAssistantOnline = payload[i] == expected[i];
    }

    if (isHomeAssistantOnline) {
      Serial.println("Home Assistant announced online; discovery republish queued");
      homeAssistantDiscoveryRequested = true;
    }
    return;
  }

  if (strcmp(receivedTopic, kStayAwakeTopic) != 0) {
    return;
  }

  bool parsedValue = false;
  if (parseStayAwakePayload(reinterpret_cast<const char*>(payload), length, parsedValue)) {
    stayAwakeRequested = parsedValue;
    stayAwakeCommandReceived = true;
    Serial.printf("Stay-awake command received on %s: %s\n",
                  kStayAwakeTopic,
                  stayAwakeRequested ? "true" : "false");
  } else {
    Serial.printf("Ignoring invalid stay-awake command payload on %s, length=%u\n", kStayAwakeTopic, length);
  }
}

bool connectMqtt() {
  logPhase("MQTT");
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  const bool bufferConfigured = mqttClient.setBufferSize(kMqttBufferSize);

  Serial.printf("MQTT server: %s:%u\n", MQTT_HOST, MQTT_PORT);
  Serial.printf("MQTT buffer size %u bytes: %s\n",
                static_cast<unsigned int>(kMqttBufferSize),
                bufferConfigured ? "ok" : "FAILED");
  Serial.printf("MQTT client ID prefix: %s\n", kTopicPrefix);
  Serial.printf("MQTT status topic: %s\n", kStatusTopic);
  Serial.printf("MQTT stay-awake topic: %s\n", kStayAwakeTopic);
  if (!bufferConfigured) {
    return false;
  }
  Serial.printf("Connecting to MQTT with timeout %lu ms\n", static_cast<unsigned long>(kMqttConnectTimeoutMs));
  const uint32_t started = millis();
  uint8_t attempt = 0;
  while (!mqttClient.connected() && millis() - started < kMqttConnectTimeoutMs) {
    ++attempt;
    const String clientId = String(kTopicPrefix) + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    Serial.printf("MQTT connect attempt %u using client ID %s\n", attempt, clientId.c_str());
    if (mqttClient.connect(
            clientId.c_str(),
            MQTT_USERNAME,
            MQTT_PASSWORD,
            kStatusTopic,
            1,
            true,
            "offline")) {
      Serial.printf("MQTT connected in %lu ms\n", static_cast<unsigned long>(millis() - started));
      const bool statusPublished = mqttClient.publish(kStatusTopic, "online", true);
      Serial.printf("MQTT publish %s = online retained: %s\n", kStatusTopic, statusPublished ? "ok" : "FAILED");
      const bool subscribed = mqttClient.subscribe(kStayAwakeTopic, 1);
      Serial.printf("MQTT subscribe %s qos=1: %s\n", kStayAwakeTopic, subscribed ? "ok" : "FAILED");
      if (!subscribed) {
        return false;
      }
      waitForRetainedStayAwakeCommand();
      const bool haSubscribed = mqttClient.subscribe(kHaStatusTopic, 0);
      Serial.printf("MQTT subscribe %s qos=0: %s\n", kHaStatusTopic, haSubscribed ? "ok" : "FAILED");
      if (!haSubscribed) {
        return false;
      }
      publishHomeAssistantDiscovery();
      return true;
    }

    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    delay(kMqttRetryDelayMs);
  }

  Serial.println("MQTT connection failed");
  return false;
}

void publishFloat(const char* suffix, float value) {
  const String fullTopic = topic(suffix);
  if (isnan(value) || isinf(value)) {
    Serial.printf("MQTT skip %s: invalid reading\n", fullTopic.c_str());
    return;
  }

  char payload[24];
  dtostrf(value, 0, 8, payload);
  const bool ok = mqttClient.publish(fullTopic.c_str(), payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", fullTopic.c_str(), payload, ok ? "ok" : "FAILED");
}

void publishText(const char* suffix, const char* value, bool retained = true) {
  const String fullTopic = topic(suffix);
  const bool ok = mqttClient.publish(fullTopic.c_str(), value, retained);
  Serial.printf("MQTT publish %s = %s retained=%s: %s\n",
                fullTopic.c_str(),
                value,
                retained ? "true" : "false",
                ok ? "ok" : "FAILED");
}

void publishDiscoveryPayload(const char* component, const char* objectId, const char* payload) {
  char discoveryTopic[160];
  if (!formatInto(discoveryTopic,
                  sizeof(discoveryTopic),
                  "Home Assistant discovery topic",
                  "%s/%s/%s/config",
                  kHaDiscoveryPrefix,
                  component,
                  objectId)) {
    Serial.printf("HA discovery skipped for %s/%s: topic too long\n", component, objectId);
    return;
  }

  const size_t payloadLength = strlen(payload);
  const size_t packetBytes = kMqttMaxHeaderBytes + 2 + strlen(discoveryTopic) + payloadLength;
  Serial.printf("HA discovery payload %s: payload=%u bytes, packet_estimate=%u/%u bytes\n",
                discoveryTopic,
                static_cast<unsigned int>(payloadLength),
                static_cast<unsigned int>(packetBytes),
                static_cast<unsigned int>(kMqttBufferSize));
  if (packetBytes > kMqttBufferSize) {
    Serial.printf("HA discovery skipped %s: MQTT packet estimate exceeds buffer\n", discoveryTopic);
    return;
  }

  const bool ok = mqttClient.publish(discoveryTopic, payload, true);
  Serial.printf("HA discovery publish %s retained: %s\n", discoveryTopic, ok ? "ok" : "FAILED");
}

void publishHaSensorDiscovery(const char* objectId,
                              const char* name,
                              const char* stateTopic,
                              const char* deviceClass,
                              const char* unit,
                              const char* stateClass,
                              const char* entityCategory) {
  char payload[1024];
  char optionalFields[260] = "";

  if (deviceClass && deviceClass[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor device_class field", ",\"device_class\":\"%s\"", deviceClass) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional device_class too long\n", objectId);
      return;
    }
  }
  if (unit && unit[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor unit field", ",\"unit_of_measurement\":\"%s\"", unit) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional unit too long\n", objectId);
      return;
    }
  }
  if (stateClass && stateClass[0]) {
    char field[64];
    if (!formatInto(field, sizeof(field), "HA sensor state_class field", ",\"state_class\":\"%s\"", stateClass) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional state_class too long\n", objectId);
      return;
    }
  }
  if (entityCategory && entityCategory[0]) {
    char field[80];
    if (!formatInto(field, sizeof(field), "HA sensor entity_category field", ",\"entity_category\":\"%s\"", entityCategory) ||
        !appendChecked(optionalFields, sizeof(optionalFields), "HA sensor optional fields", field)) {
      Serial.printf("HA discovery skipped for %s: optional entity_category too long\n", objectId);
      return;
    }
  }

  if (!formatInto(payload,
                  sizeof(payload),
                  "HA sensor discovery payload",
                  "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\"%s,"
                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"},"
                  "\"origin\":{\"name\":\"%s\",\"sw_version\":\"%s\"}}",
                  name,
                  objectId,
                  stateTopic,
                  optionalFields,
                  kHaDeviceIdentifier,
                  kHaDeviceName,
                  kHaManufacturer,
                  kHaModel,
                  kFirmwareName,
                  kFirmwareName,
                  kFirmwareName)) {
    Serial.printf("HA discovery skipped for %s: payload too long\n", objectId);
    return;
  }

  publishDiscoveryPayload("sensor", objectId, payload);
}

void publishHaSwitchDiscovery() {
  char payload[1024];
  if (!formatInto(payload,
                  sizeof(payload),
                  "HA switch discovery payload",
                  "{\"name\":\"Stay Awake\",\"unique_id\":\"esp32_meteo_v3_stay_awake\","
                  "\"command_topic\":\"%s\",\"state_topic\":\"%s\","
                  "\"payload_on\":\"true\",\"payload_off\":\"false\","
                  "\"state_on\":\"true\",\"state_off\":\"false\",\"retain\":true,"
                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"},"
                  "\"origin\":{\"name\":\"%s\",\"sw_version\":\"%s\"}}",
                  kStayAwakeTopic,
                  kStayAwakeTopic,
                  kHaDeviceIdentifier,
                  kHaDeviceName,
                  kHaManufacturer,
                  kHaModel,
                  kFirmwareName,
                  kFirmwareName,
                  kFirmwareName)) {
    Serial.println("HA discovery skipped for esp32_meteo_v3_stay_awake: payload too long");
    return;
  }

  publishDiscoveryPayload("switch", "esp32_meteo_v3_stay_awake", payload);
}

void publishHomeAssistantDiscovery() {
  homeAssistantDiscoveryRequested = false;
  logPhase("Home Assistant discovery");
  Serial.printf("Publishing retained discovery configs under %s/#\n", kHaDiscoveryPrefix);

  publishHaSensorDiscovery("esp32_meteo_v3_bmp390_temperature",
                           "BMP390 Temperature",
                           topic("/sensor/bmp390_temperature").c_str(),
                           "temperature",
                           "\\u00b0C",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_absolute_pressure",
                           "Absolute Pressure",
                           topic("/sensor/absolute_pressure").c_str(),
                           "atmospheric_pressure",
                           "hPa",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_outside_temperature",
                           "Outside Temperature",
                           topic("/sensor/outside_temperature").c_str(),
                           "temperature",
                           "\\u00b0C",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_outside_humidity",
                           "Outside Humidity",
                           topic("/sensor/outside_humidity").c_str(),
                           "humidity",
                           "%",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_battery_voltage",
                           "Battery Voltage",
                           topic("/sensor/battery_voltage").c_str(),
                           "voltage",
                           "V",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_battery_current",
                           "Battery Current",
                           topic("/sensor/battery_current").c_str(),
                           "current",
                           "mA",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_battery_power",
                           "Battery Power",
                           topic("/sensor/battery_power").c_str(),
                           "power",
                           "W",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_battery_level",
                           "Battery Level",
                           topic("/sensor/battery_level").c_str(),
                           "battery",
                           "%",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_solar_raw_voltage",
                           "Solar Raw Voltage",
                           topic("/sensor/solar_raw_voltage").c_str(),
                           "voltage",
                           "V",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_solar_panel_current",
                           "Solar Panel Current",
                           topic("/sensor/solar_panel_current").c_str(),
                           "current",
                           "mA",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_solar_raw_power",
                           "Solar Raw Power",
                           topic("/sensor/solar_raw_power").c_str(),
                           "power",
                           "W",
                           "measurement",
                           "");
  publishHaSensorDiscovery("esp32_meteo_v3_wifi_signal",
                           "WiFi Signal",
                           topic("/diagnostic/wifi_signal").c_str(),
                           "signal_strength",
                           "dBm",
                           "measurement",
                           "diagnostic");
  publishHaSensorDiscovery("esp32_meteo_v3_wifi_ssid",
                           "WiFi SSID",
                           topic("/diagnostic/wifi_ssid").c_str(),
                           "",
                           "",
                           "",
                           "diagnostic");
  publishHaSensorDiscovery("esp32_meteo_v3_ip_address",
                           "IP Address",
                           topic("/diagnostic/ip_address").c_str(),
                           "",
                           "",
                           "",
                           "diagnostic");
  publishHaSensorDiscovery("esp32_meteo_v3_status",
                           "Status",
                           kStatusTopic,
                           "",
                           "",
                           "",
                           "diagnostic");
  publishHaSwitchDiscovery();
}

void publishDiagnostics() {
  Serial.println("Publishing diagnostics");
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI());
  publishText("/diagnostic/wifi_signal", rssi);
  const String wifiSsid = WiFi.SSID();
  publishText("/diagnostic/wifi_ssid", wifiSsid.c_str());
  const String ipAddress = WiFi.localIP().toString();
  publishText("/diagnostic/ip_address", ipAddress.c_str());
}

void publishDeviceStatus() {
  String status = "online";
  if (!devices.solarInaReady || !devices.batteryInaReady || !devices.sht41Ready || !devices.bmp390Ready) {
    status += "; degraded:";
    if (!devices.solarInaReady) {
      status += " solar_ina226";
    }
    if (!devices.batteryInaReady) {
      status += " battery_ina226";
    }
    if (!devices.sht41Ready) {
      status += " sht4x";
    }
    if (!devices.bmp390Ready) {
      status += " bmp3xx";
    }
  }
  const bool ok = mqttClient.publish(kStatusTopic, status.c_str(), true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", kStatusTopic, status.c_str(), ok ? "ok" : "FAILED");
}

void publishReadings() {
  logPhase("MQTT publish cycle");
  Reading reading = readSensors();

  publishFloat("/sensor/bmp390_temperature", reading.bmpTemperatureC);
  publishFloat("/sensor/absolute_pressure", reading.absolutePressureHpa);
  publishFloat("/sensor/outside_temperature", reading.outsideTemperatureC);
  publishFloat("/sensor/outside_humidity", reading.outsideHumidityPercent);
  publishFloat("/sensor/battery_voltage", reading.batteryVoltageV);
  publishFloat("/sensor/battery_current", reading.batteryCurrentMa);
  publishFloat("/sensor/battery_power", reading.batteryPowerW);
  publishFloat("/sensor/battery_level", reading.batteryLevelPercent);
  publishFloat("/sensor/solar_raw_voltage", reading.solarRawVoltageV);
  publishFloat("/sensor/solar_panel_current", reading.solarPanelCurrentMa);
  publishFloat("/sensor/solar_raw_power", reading.solarRawPowerW);
  publishDiagnostics();
  publishDeviceStatus();
  flushMqtt(kTelemetryFlushMs);
}

void flushMqtt(uint32_t durationMs) {
  Serial.printf("Flushing MQTT for %lu ms\n", static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    ArduinoOTA.handle();
    mqttClient.loop();
    delay(10);
  }
  Serial.println("MQTT flush complete");
}

void sleepForDefaultInterval(const char* reason) {
  logPhase("Deep sleep");
  while (otaInProgress) {
    Serial.println("OTA update in progress; delaying deep sleep");
    ArduinoOTA.handle();
    mqttClient.loop();
    delay(100);
  }
  Serial.printf("Entering deep sleep for %llu seconds: %s\n", kDeepSleepSeconds, reason);
  if (mqttClient.connected()) {
    const bool statusOk = mqttClient.publish(kStatusTopic, "sleeping", true);
    Serial.printf("MQTT publish %s = sleeping retained: %s\n", kStatusTopic, statusOk ? "ok" : "FAILED");
    flushMqtt(kMqttFlushMs);
    mqttClient.disconnect();
    Serial.println("MQTT disconnected");
  } else {
    Serial.println("MQTT not connected; skipping sleeping status publish");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disconnected and radio turned off");
  esp_sleep_enable_timer_wakeup(kDeepSleepSeconds * 1000000ULL);
  Serial.println("Deep-sleep timer configured");
  Serial.flush();
  esp_deep_sleep_start();
}

void waitForRetainedStayAwakeCommand() {
  logPhase("Stay-awake command");
  Serial.println("MQTT commands cannot wake the ESP32 while it is already in deep sleep.");
  Serial.println("A retained stay-awake command will be applied after the next timer wake and MQTT reconnect.");

  if (kForceStayAwakeForTesting) {
    stayAwakeRequested = true;
    stayAwakeCommandReceived = true;
    Serial.println("Local force stay-awake flag is enabled; bypassing retained MQTT command wait");
    return;
  }

  Serial.printf("Waiting up to %lu ms for retained command on %s\n",
                static_cast<unsigned long>(kRetainedCommandWaitMs),
                kStayAwakeTopic);
  const uint32_t started = millis();
  while (!stayAwakeCommandReceived && millis() - started < kRetainedCommandWaitMs) {
    ArduinoOTA.handle();
    mqttClient.loop();
    delay(10);
  }

  if (!stayAwakeCommandReceived) {
    Serial.println("No retained stay-awake command received before timeout; defaulting to one publish then sleep");
  } else {
    Serial.printf("Retained stay-awake command applied: %s\n", stayAwakeRequested ? "true" : "false");
    Serial.printf("Stay-awake retained value means this boot will %s\n",
                  stayAwakeRequested ? "remain awake" : "return to deep sleep after publishing");
  }

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }
}

void appSetup() {
  Serial.begin(kSerialBaud);
  delay(200);

  Serial.println();
  Serial.printf("%s boot\n", kFirmwareName);
  Serial.printf("Reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.printf("MQTT topic prefix: %s\n", kTopicPrefix);
  Serial.printf("Stay-awake publish interval: %lu ms\n", static_cast<unsigned long>(kStayAwakePublishIntervalMs));
  Serial.printf("Default deep sleep: %llu seconds\n", kDeepSleepSeconds);
  Serial.printf("Local force stay-awake flag: %s\n", kForceStayAwakeForTesting ? "enabled" : "disabled");
  Serial.println("Safety: I2C pullups must be tied to 3.3 V only.");
  Serial.println("Safety: TP5000 Li-ion output above about 4.25 V is unsafe/untrusted.");
  Serial.println("Safety: solar panel rating, TP5000 variant, charge current, and ESP32 regulator behavior are not assumed.");

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Serial.printf("Waiting %lu ms for I2C peripherals to stabilize after wake\n",
                static_cast<unsigned long>(kI2cPowerStabilizeDelayMs));
  delay(kI2cPowerStabilizeDelayMs);
  scanI2cBus();
  initializeSensors();

  if (!connectWifi()) {
    sleepForDefaultInterval("WiFi unavailable");
  }

  initializeOta();

  if (!connectMqtt()) {
    sleepForDefaultInterval("MQTT unavailable");
  }

  Serial.printf("Waiting %lu ms before first sensor read after boot/wake\n",
                static_cast<unsigned long>(kBeforeFirstReadDelayMs));
  delay(kBeforeFirstReadDelayMs);
  publishReadings();
  lastPublishMs = millis();

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake mode is disabled; device will sleep after first publish");
    sleepForDefaultInterval("stay_awake disabled or absent");
  }

  Serial.printf("Stay-awake mode enabled; publishing every %lu ms\n",
                static_cast<unsigned long>(kStayAwakePublishIntervalMs));
}

void appLoop() {
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {
    Serial.printf("Connection lost while awake: wifi_status=%d mqtt_connected=%s\n",
                  WiFi.status(),
                  yesNo(mqttClient.connected()));
    sleepForDefaultInterval("connection lost");
  }

  mqttClient.loop();
  ArduinoOTA.handle();

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }

  if (!stayAwakeRequested) {
    Serial.println("Stay-awake changed to false while awake; publishing once before sleep");
    publishReadings();
    sleepForDefaultInterval("stay_awake disabled");
  }

  if (millis() - lastPublishMs >= kStayAwakePublishIntervalMs) {
    Serial.printf("Stay-awake publish interval reached after %lu ms\n",
                  static_cast<unsigned long>(millis() - lastPublishMs));
    publishReadings();
    lastPublishMs = millis();
  }

  delay(20);
}

}  // namespace

void setup() {
  appSetup();
}

void loop() {
  appLoop();
}
