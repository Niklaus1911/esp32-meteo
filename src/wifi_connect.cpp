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
    Serial.println("WiFi static IP not configured; using DHCP");
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;

  if (!localIp.fromString(config.staticIp) ||
      !gateway.fromString(config.gateway) ||
      !subnet.fromString(config.subnet)) {
    Serial.println("WiFi static IP runtime config is invalid; falling back to DHCP");
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

}  // namespace

bool connectWifi() {
  logPhase("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(kWifiTxPower);
  Serial.printf("WiFi TX power requested: %s\n", kWifiTxPowerLabel);
  configureStaticWifiIp();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi already connected, SSID %s, RSSI %d dBm, IP %s\n",
                  WiFi.SSID().c_str(),
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
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
    Serial.printf("WiFi connected in %lu ms, SSID %s, RSSI %d dBm, IP %s\n",
                  static_cast<unsigned long>(millis() - started),
                  WiFi.SSID().c_str(),
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.printf("Saved WiFi connection failed after %lu ms, status=%d; sleeping instead of opening portal\n",
                static_cast<unsigned long>(millis() - started),
                WiFi.status());
  WiFi.disconnect(false);
  return false;
}

}  // namespace Esp32Meteo
