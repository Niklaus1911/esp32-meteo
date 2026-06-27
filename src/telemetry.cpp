#include "telemetry.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "battery.h"
#include "config.h"
#include "firmware_logic.h"
#include "mqtt_client.h"
#include "runtime_config.h"
#include "runtime_state.h"
#include "sensors.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void markTelemetryPublishCompleted() {
  telemetryPublishCompleted = true;
  telemetryPublishCompletedMs = millis();
  Serial.printf("%sTelemetry publish completed%s at %s%lu ms%s; post-telemetry awake window starts now\n",
                serialStyle(SerialStyle::Success),
                serialReset(),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned long>(telemetryPublishCompletedMs),
                serialReset());
}

bool publishDiagnostics() {
  Serial.println("Publishing diagnostics");
  const DeviceState& devices = deviceState();
  bool allOk = true;
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI());
  allOk &= publishText("/diagnostic/wifi_signal", rssi);
  const String wifiSsid = WiFi.SSID();
  allOk &= publishText("/diagnostic/wifi_ssid", wifiSsid.c_str());
  const String ipAddress = WiFi.localIP().toString();
  allOk &= publishText("/diagnostic/ip_address", ipAddress.c_str());
  allOk &= publishText("/diagnostic/reset_reason", resetReasonName(esp_reset_reason()));
  allOk &= publishText("/diagnostic/battery_chemistry",
                       batteryChemistryName(runtimeConfig().batteryChemistryId));

  char sensorReadiness[160];
  if (formatSensorReadiness(sensorReadiness,
                            sizeof(sensorReadiness),
                            {devices.solarInaReady, devices.solarInaIssue},
                            {devices.batteryInaReady, devices.batteryInaIssue},
                            {devices.sht41Ready, devices.sht41Issue},
                            {devices.bmp390Ready, devices.bmp390Issue})) {
    allOk &= publishText("/diagnostic/sensor_readiness", sensorReadiness);
  } else {
    Serial.printf("%sSkipping sensor readiness diagnostic%s: formatted payload too long\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    allOk = false;
  }

  return allOk;
}

bool publishDeviceStatus() {
  char status[192];
  const DeviceState& devices = deviceState();
  if (!formatDeviceStatus(status,
                          sizeof(status),
                          devices.solarInaIssue,
                          devices.batteryInaIssue,
                          devices.sht41Issue,
                          devices.bmp390Issue)) {
    Serial.printf("%sMQTT status publish skipped%s: status payload too long\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    return false;
  }

  const bool ok = mqttClient().publish(kStatusTopic, status, true);
  Serial.printf("MQTT publish %s%s%s = %s%s%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                kStatusTopic,
                serialReset(),
                serialStyle(ok ? SerialStyle::Value : SerialStyle::Warning),
                status,
                serialReset(),
                serialOkFailed(ok));
  return ok;
}

void waitBetweenSensorGroups() {
  waitWithMqttAndOta(kSensorGroupPublishGapMs, "Sensor group publish gap");
}

bool publishBmp390Group() {
  logBootPhase("sensor_bmp390_start");
  const Bmp390Reading reading = readBmp390Sensor();
  bool allOk = true;
  allOk &= publishFloat("/sensor/bmp390_temperature", reading.temperatureC);
  allOk &= publishFloat("/sensor/absolute_pressure", reading.absolutePressureHpa);
  publishBootPhase(allOk ? "sensor_bmp390_done" : "sensor_bmp390_failed");
  return allOk;
}

bool publishSht41Group() {
  logBootPhase("sensor_sht41_start");
  const Sht41Reading reading = readSht41Sensor();
  bool allOk = true;
  allOk &= publishFloat("/sensor/outside_temperature", reading.temperatureC);
  allOk &= publishFloat("/sensor/outside_humidity", reading.humidityPercent);
  publishBootPhase(allOk ? "sensor_sht41_done" : "sensor_sht41_failed");
  return allOk;
}

bool publishBatteryGroup() {
  logBootPhase("sensor_battery_ina226_start");
  const Ina226Reading reading = readBatteryIna226();
  bool allOk = true;
  allOk &= publishFloat("/sensor/battery_voltage", reading.voltageV);
  allOk &= publishFloat("/sensor/battery_current", reading.currentMa);
  allOk &= publishFloat("/sensor/battery_power", reading.powerW);
  allOk &= publishFloat("/sensor/battery_level", batteryLevelPercent(reading.voltageV));
  publishBootPhase(allOk ? "sensor_battery_ina226_done" : "sensor_battery_ina226_failed");
  return allOk;
}

bool publishSolarGroup() {
  logBootPhase("sensor_solar_ina226_start");
  const Ina226Reading reading = readSolarIna226();
  bool allOk = true;
  allOk &= publishFloat("/sensor/solar_raw_voltage", reading.voltageV);
  allOk &= publishFloat("/sensor/solar_panel_current", reading.currentMa);
  allOk &= publishFloat("/sensor/solar_raw_power", reading.powerW);
  publishBootPhase(allOk ? "sensor_solar_ina226_done" : "sensor_solar_ina226_failed");
  return allOk;
}

}  // namespace

bool publishReadings() {
  logPhase("MQTT staggered publish cycle");
  logBootPhase("telemetry_start");
  bool allOk = true;

  allOk &= publishBmp390Group();
  waitBetweenSensorGroups();
  allOk &= publishSht41Group();
  waitBetweenSensorGroups();
  allOk &= publishBatteryGroup();
  waitBetweenSensorGroups();
  allOk &= publishSolarGroup();

  logBootPhase("diagnostics_start");
  allOk &= publishDiagnostics();
  allOk &= publishDeviceStatus();
  publishBootPhase(allOk ? "telemetry_done" : "telemetry_failed");
  flushMqtt(kTelemetryFlushMs);
  markTelemetryPublishCompleted();
  Serial.printf("MQTT publish cycle result: %s\n", serialCompleteFailed(allOk));
  return allOk;
}

}  // namespace Esp32Meteo
