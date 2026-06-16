#include "telemetry.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "firmware_logic.h"
#include "mqtt_client.h"
#include "runtime_state.h"
#include "sensors.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void markTelemetryPublishCompleted() {
  telemetryPublishCompleted = true;
  telemetryPublishCompletedMs = millis();
  Serial.printf("Telemetry publish completed at %lu ms; post-telemetry awake window starts now\n",
                static_cast<unsigned long>(telemetryPublishCompletedMs));
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
  allOk &= publishText("/diagnostic/battery_chemistry", BATTERY_CHEMISTRY_NAME);

  char sensorReadiness[160];
  if (formatSensorReadiness(sensorReadiness,
                            sizeof(sensorReadiness),
                            {devices.solarInaReady, devices.solarInaIssue},
                            {devices.batteryInaReady, devices.batteryInaIssue},
                            {devices.sht41Ready, devices.sht41Issue},
                            {devices.bmp390Ready, devices.bmp390Issue})) {
    allOk &= publishText("/diagnostic/sensor_readiness", sensorReadiness);
  } else {
    Serial.println("Skipping sensor readiness diagnostic: formatted payload too long");
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
    Serial.println("MQTT status publish skipped: status payload too long");
    return false;
  }

  const bool ok = mqttClient().publish(kStatusTopic, status, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", kStatusTopic, status, ok ? "ok" : "FAILED");
  return ok;
}

}  // namespace

bool publishReadings() {
  logPhase("MQTT publish cycle");
  Reading reading = readSensors();
  bool allOk = true;

  allOk &= publishFloat("/sensor/bmp390_temperature", reading.bmpTemperatureC);
  allOk &= publishFloat("/sensor/absolute_pressure", reading.absolutePressureHpa);
  allOk &= publishFloat("/sensor/outside_temperature", reading.outsideTemperatureC);
  allOk &= publishFloat("/sensor/outside_humidity", reading.outsideHumidityPercent);
  allOk &= publishFloat("/sensor/battery_voltage", reading.batteryVoltageV);
  allOk &= publishFloat("/sensor/battery_current", reading.batteryCurrentMa);
  allOk &= publishFloat("/sensor/battery_power", reading.batteryPowerW);
  allOk &= publishFloat("/sensor/battery_level", reading.batteryLevelPercent);
  allOk &= publishFloat("/sensor/solar_raw_voltage", reading.solarRawVoltageV);
  allOk &= publishFloat("/sensor/solar_panel_current", reading.solarPanelCurrentMa);
  allOk &= publishFloat("/sensor/solar_raw_power", reading.solarRawPowerW);
  allOk &= publishDiagnostics();
  allOk &= publishDeviceStatus();
  flushMqtt(kTelemetryFlushMs);
  markTelemetryPublishCompleted();
  Serial.printf("MQTT publish cycle result: %s\n", allOk ? "complete" : "FAILED");
  return allOk;
}

}  // namespace Esp32Meteo
