#include "provisioning.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdlib>

#include "config.h"
#include "provisioning_logic.h"
#include "runtime_config.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

bool parsePortValue(const char* value, uint32_t& port) {
  if (!value || !value[0]) {
    return false;
  }

  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }

  port = static_cast<uint32_t>(parsed);
  return isValidMqttPort(port);
}

void formatSetupApName(char* buffer, size_t bufferSize) {
  const uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  snprintf(buffer, bufferSize, "ESP32-Meteo-Setup-%06X", chipId);
}

void formatIpModeSelect(char* buffer, size_t bufferSize, bool staticSelected) {
  snprintf(buffer,
           bufferSize,
           "<br/><label for='ip_mode_select'>IP mode</label>"
           "<select id='ip_mode_select'>"
           "<option value='dhcp'%s>DHCP</option>"
           "<option value='static'%s>Static</option>"
           "</select>\n"
           "<script>"
           "document.addEventListener('DOMContentLoaded',function(){"
           "var m=document.getElementById('ip_mode_select');"
           "var h=document.getElementById('ip_mode');"
           "var ip=document.getElementById('static_ip');"
           "var gw=document.getElementById('gateway');"
           "var sn=document.getElementById('subnet');"
           "function u(){"
           "if(!m||!h||!ip||!gw||!sn)return;"
           "if(m.value==='dhcp'){ip.value='';gw.value='';sn.value='';}"
           "else if(!sn.value){sn.value='%s';}"
           "h.value=m.value;"
           "}"
           "if(m)m.addEventListener('change',u);"
           "u();"
           "});"
           "</script>",
           staticSelected ? "" : " selected",
           staticSelected ? " selected" : "",
           kRuntimeConfigDefaultSubnet);
}

void formatBatteryChemistrySelect(char* buffer, size_t bufferSize, uint8_t chemistryId) {
  const bool lifepo4Selected = chemistryId == kRuntimeBatteryChemistryLiFePO4;
  snprintf(buffer,
           bufferSize,
           "<br/><label for='batt_chem_select'>Battery chemistry</label>"
           "<select id='batt_chem_select'>"
           "<option value='li_ion'%s>Li-ion</option>"
           "<option value='lifepo4'%s>LiFePO4</option>"
           "</select>\n"
           "<script>"
           "document.addEventListener('DOMContentLoaded',function(){"
           "var s=document.getElementById('batt_chem_select');"
           "var h=document.getElementById('batt_chem');"
           "function u(){if(s&&h)h.value=s.value;}"
           "if(s)s.addEventListener('change',u);"
           "u();"
           "});"
           "</script>",
           lifepo4Selected ? "" : " selected",
           lifepo4Selected ? " selected" : "");
}

bool runProvisioningPortal() {
  logPhase("Provisioning");
  Serial.printf("%sStarting WiFiManager captive portal%s for WiFi and runtime settings\n",
                serialStyle(SerialStyle::Highlight),
                serialReset());
  Serial.println("MQTT and OTA passwords are accepted by the portal but will not be printed");

  const RuntimeConfig& existing = runtimeConfig();
  char mqttPort[6];
  snprintf(mqttPort, sizeof(mqttPort), "%u", existing.mqttPort ? existing.mqttPort : kRuntimeConfigDefaultMqttPort);
  const char* subnetDefault = existing.hasStaticIp ? provisioningSubnetForStaticMode(existing.subnet) : "";
  const char* selectedIpMode = provisioningIpModeForConfig(existing.hasStaticIp);
  const char* selectedBatteryChemistry = batteryChemistryKey(existing.batteryChemistryId);

  char ipModeSelectHtml[1024];
  formatIpModeSelect(ipModeSelectHtml, sizeof(ipModeSelectHtml), existing.hasStaticIp);
  char batteryChemistrySelectHtml[512];
  formatBatteryChemistrySelect(batteryChemistrySelectHtml,
                               sizeof(batteryChemistrySelectHtml),
                               existing.batteryChemistryId);

  WiFiManagerParameter mqttHostParam("mqtt_host",
                                     "MQTT host",
                                     existing.mqttHost,
                                     kRuntimeConfigMqttHostMaxLength);
  WiFiManagerParameter mqttPortParam("mqtt_port",
                                     "MQTT port",
                                     mqttPort,
                                     sizeof(mqttPort) - 1,
                                     "type=\"number\" min=\"1\" max=\"65535\"");
  WiFiManagerParameter mqttUserParam("mqtt_user",
                                     "MQTT username",
                                     existing.mqttUsername,
                                     kRuntimeConfigMqttUsernameMaxLength);
  WiFiManagerParameter mqttPassParam("mqtt_pass",
                                     "MQTT password",
                                     existing.mqttPassword,
                                     kRuntimeConfigMqttPasswordMaxLength,
                                     "type=\"password\"");
  WiFiManagerParameter otaPassParam("ota_pass",
                                    "OTA password",
                                    existing.otaPassword,
                                    kRuntimeConfigOtaPasswordMaxLength,
                                    "type=\"password\"");
  WiFiManagerParameter ipModeSelectParam(ipModeSelectHtml);
  WiFiManagerParameter batteryChemistrySelectParam(batteryChemistrySelectHtml);
  WiFiManagerParameter ipModeParam("ip_mode",
                                   "IP mode",
                                   selectedIpMode,
                                   7,
                                   "type=\"hidden\"",
                                   WFM_NO_LABEL);
  WiFiManagerParameter batteryChemistryParam("batt_chem",
                                             "Battery chemistry",
                                             selectedBatteryChemistry,
                                             12,
                                             "type=\"hidden\"",
                                             WFM_NO_LABEL);
  WiFiManagerParameter staticIpParam("static_ip",
                                     "Static IP (Static mode)",
                                     existing.staticIp,
                                     kRuntimeConfigIpv4MaxLength);
  WiFiManagerParameter gatewayParam("gateway",
                                    "Gateway (Static mode)",
                                    existing.gateway,
                                    kRuntimeConfigIpv4MaxLength);
  WiFiManagerParameter subnetParam("subnet",
                                   "Subnet (Static mode)",
                                   subnetDefault,
                                   kRuntimeConfigIpv4MaxLength);

  WiFiManager manager;
  manager.setConnectTimeout(kWifiConnectTimeoutMs / 1000);
  manager.setDebugOutput(false);
  manager.addParameter(&mqttHostParam);
  manager.addParameter(&mqttPortParam);
  manager.addParameter(&mqttUserParam);
  manager.addParameter(&mqttPassParam);
  manager.addParameter(&otaPassParam);
  manager.addParameter(&ipModeSelectParam);
  manager.addParameter(&ipModeParam);
  manager.addParameter(&batteryChemistrySelectParam);
  manager.addParameter(&batteryChemistryParam);
  manager.addParameter(&staticIpParam);
  manager.addParameter(&gatewayParam);
  manager.addParameter(&subnetParam);

  char apName[40];
  formatSetupApName(apName, sizeof(apName));
  Serial.printf("Provisioning AP: %s%s%s, portal has no timeout\n",
                serialStyle(SerialStyle::Topic),
                apName,
                serialReset());

  if (!manager.startConfigPortal(apName)) {
    Serial.printf("%sProvisioning portal exited without a saved WiFi connection%s\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset());
    WiFi.disconnect(false);
    return false;
  }

  uint32_t parsedMqttPort = 0;
  if (!parsePortValue(mqttPortParam.getValue(), parsedMqttPort)) {
    Serial.printf("%sProvisioning rejected%s: MQTT port must be between 1 and 65535\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    WiFi.disconnect(false);
    return false;
  }

  const bool staticMode = provisioningIpModeIsStatic(ipModeParam.getValue());
  const char* configuredStaticIp = staticMode ? staticIpParam.getValue() : provisioningStaticFieldForDhcpMode();
  const char* configuredGateway = staticMode ? gatewayParam.getValue() : provisioningStaticFieldForDhcpMode();
  const char* configuredSubnet = staticMode ? provisioningSubnetForStaticMode(subnetParam.getValue())
                                            : provisioningStaticFieldForDhcpMode();

  RuntimeConfig configured;
  if (!populateRuntimeConfig(configured,
                             mqttHostParam.getValue(),
                             parsedMqttPort,
                             mqttUserParam.getValue(),
                             mqttPassParam.getValue(),
                             otaPassParam.getValue(),
                             batteryChemistryParam.getValue(),
                             configuredStaticIp,
                             configuredGateway,
                             configuredSubnet)) {
    Serial.printf("%sProvisioning rejected%s: one or more values exceed firmware limits\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
    WiFi.disconnect(false);
    return false;
  }

  const RuntimeConfigValidation validation = validateRuntimeConfig(configured);
  if (!validation.valid) {
    Serial.printf("%sProvisioning rejected%s: %s\n",
                  serialStyle(SerialStyle::Error),
                  serialReset(),
                  validation.reason);
    WiFi.disconnect(false);
    return false;
  }

  if (!saveRuntimeConfig(configured)) {
    WiFi.disconnect(false);
    return false;
  }

  Serial.printf("%sProvisioning saved%s; rebooting so normal boot uses stored runtime settings\n",
                serialStyle(SerialStyle::Success),
                serialReset());
  Serial.flush();
  ESP.restart();
  return false;
}

}  // namespace

bool ensureRuntimeProvisioning() {
  const bool configLoaded = loadRuntimeConfig();
  WiFi.mode(WIFI_STA);

  if (configLoaded) {
    return true;
  }

  return runProvisioningPortal();
}

}  // namespace Esp32Meteo
