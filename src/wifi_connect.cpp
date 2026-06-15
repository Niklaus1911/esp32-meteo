#include "wifi_connect.h"

#include <WiFi.h>

#include "config.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

bool configureStaticWifiIp() {
  if (!kWifiHasStaticIp) {
    Serial.println("WiFi static IP not configured; using DHCP");
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;

  if (!localIp.fromString(kWifiStaticIp) || !gateway.fromString(kWifiGateway) || !subnet.fromString(WIFI_SUBNET)) {
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

}  // namespace

bool connectWifi() {
  logPhase("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(kWifiTxPower);
  Serial.printf("WiFi TX power requested: %s\n", kWifiTxPowerLabel);
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

}  // namespace Esp32Meteo
