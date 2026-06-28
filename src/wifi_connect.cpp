#include "wifi_connect.h"

#include <WiFi.h>

#include "config.h"
#include "local_button.h"
#include "runtime_config.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

bool configureStaticWifiIp() {
  const RuntimeConfig& config = runtimeConfig();
  if (!config.hasStaticIp) {
    Serial.printf("WiFi static IP %s; using DHCP\n", serialEnabledDisabled(false));
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;

  if (!localIp.fromString(config.staticIp) ||
      !gateway.fromString(config.gateway) ||
      !subnet.fromString(config.subnet)) {
    Serial.printf("%sWiFi static IP runtime config is invalid%s; WiFi connection will be skipped\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    return false;
  }

  const bool configured = WiFi.config(localIp, gateway, subnet);
  Serial.printf("WiFi static IP config %s%s%s, IP %s%s%s, gateway %s%s%s, subnet %s%s%s\n",
                serialStyle(configured ? SerialStyle::Success : SerialStyle::Error),
                configured ? "applied" : "FAILED",
                serialReset(),
                serialStyle(SerialStyle::Value),
                localIp.toString().c_str(),
                serialReset(),
                serialStyle(SerialStyle::Value),
                gateway.toString().c_str(),
                serialReset(),
                serialStyle(SerialStyle::Value),
                subnet.toString().c_str(),
                serialReset());
  return configured;
}

}  // namespace

bool connectWifi() {
  logPhase("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(kWifiTxPower);
  Serial.printf("WiFi TX power requested: %s%s%s\n",
                serialStyle(SerialStyle::Value),
                kWifiTxPowerLabel,
                serialReset());
  if (!configureStaticWifiIp()) {
    Serial.printf("%sWiFi static IP setup failed%s; static IP is required by runtime config\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    WiFi.disconnect(false);
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("%sWiFi already connected%s, SSID %s%s%s, RSSI %s%d dBm%s, IP %s%s%s\n",
                  serialStyle(SerialStyle::Success),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.SSID().c_str(),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.RSSI(),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.localIP().toString().c_str(),
                  serialReset());
    return true;
  }

  Serial.printf("Connecting to saved WiFi credentials with timeout %lu ms\n",
                static_cast<unsigned long>(kWifiConnectTimeoutMs));
  Serial.println("WiFi password will not be printed");
  Serial.printf("Waiting %lu ms before WiFi.begin() to reduce wake current step\n",
                static_cast<unsigned long>(kPreWifiStartStabilizeMs));
  const uint32_t preWifiStarted = millis();
  while (millis() - preWifiStarted < kPreWifiStartStabilizeMs) {
    serviceLocalButton();
    delay(10);
  }
  WiFi.begin();

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiConnectTimeoutMs) {
    serviceLocalButton();
    delay(kWifiRetryDelayMs);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("%sWiFi connected%s in %s%lu ms%s, SSID %s%s%s, RSSI %s%d dBm%s, IP %s%s%s\n",
                  serialStyle(SerialStyle::Success),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  static_cast<unsigned long>(millis() - started),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.SSID().c_str(),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.RSSI(),
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  WiFi.localIP().toString().c_str(),
                  serialReset());
    return true;
  }

  Serial.printf("%sSaved WiFi connection failed%s after %s%lu ms%s, status=%d; sleeping instead of opening portal\n",
                serialStyle(SerialStyle::Error),
                serialReset(),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned long>(millis() - started),
                serialReset(),
                WiFi.status());
  WiFi.disconnect(false);
  return false;
}

}  // namespace Esp32Meteo
