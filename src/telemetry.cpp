#include "telemetry.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
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

const char* readinessText(bool ready, const char* issue) {
  if (ready) {
    return "ready";
  }
  if (issue && issue[0]) {
    return issue;
  }
  return "unknown";
}

void publishDiagnostics() {
  Serial.println("Publishing diagnostics");
  const DeviceState& devices = deviceState();
  char rssi[16];
  snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI());
  publishText("/diagnostic/wifi_signal", rssi);
  const String wifiSsid = WiFi.SSID();
  publishText("/diagnostic/wifi_ssid", wifiSsid.c_str());
  const String ipAddress = WiFi.localIP().toString();
  publishText("/diagnostic/ip_address", ipAddress.c_str());
  publishText("/diagnostic/reset_reason", resetReasonName(esp_reset_reason()));
  publishText("/diagnostic/battery_chemistry", BATTERY_CHEMISTRY_NAME);

  char sensorReadiness[160];
  if (formatInto(sensorReadiness,
                 sizeof(sensorReadiness),
                 "sensor readiness diagnostic",
                 "solar=%s battery=%s sht4x=%s bmp3xx=%s",
                 readinessText(devices.solarInaReady, devices.solarInaIssue),
                 readinessText(devices.batteryInaReady, devices.batteryInaIssue),
                 readinessText(devices.sht41Ready, devices.sht41Issue),
                 readinessText(devices.bmp390Ready, devices.bmp390Issue))) {
    publishText("/diagnostic/sensor_readiness", sensorReadiness);
  }
}

void appendDegradedIssue(String& status, bool& degraded, const char* issue) {
  if (!issue || !issue[0]) {
    return;
  }
  if (!degraded) {
    status += "; degraded:";
    degraded = true;
  }
  status += ' ';
  status += issue;
}

void publishDeviceStatus() {
  String status = "online";
  bool degraded = false;
  const DeviceState& devices = deviceState();
  appendDegradedIssue(status, degraded, devices.solarInaIssue);
  appendDegradedIssue(status, degraded, devices.batteryInaIssue);
  appendDegradedIssue(status, degraded, devices.sht41Issue);
  appendDegradedIssue(status, degraded, devices.bmp390Issue);
  const bool ok = mqttClient().publish(kStatusTopic, status.c_str(), true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", kStatusTopic, status.c_str(), ok ? "ok" : "FAILED");
}

}  // namespace

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
  markTelemetryPublishCompleted();
}

}  // namespace Esp32Meteo
