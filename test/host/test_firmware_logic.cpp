#include <cassert>
#include <cmath>
#include <cstring>

#include "battery_curve.h"
#include "firmware_logic.h"
#include "local_button_logic.h"
#include "provisioning_logic.h"
#include "runtime_config.h"
#include "serial_style.h"

using namespace Esp32Meteo;

namespace {

void testStayAwakeParsing() {
  bool value = false;
  assert(parseStayAwakePayload(" true\n", 6, value));
  assert(value);

  value = true;
  assert(parseStayAwakePayload(" OFF ", 5, value));
  assert(!value);

  assert(!parseStayAwakePayload("maybe", 5, value));
  assert(!parseStayAwakePayload("", 0, value));
}

void testResetCredentialsParsing() {
  assert(parseResetCredentialsPayload("reset", 5));
  assert(!parseResetCredentialsPayload(" reset", 6));
  assert(!parseResetCredentialsPayload("reset\n", 6));
  assert(!parseResetCredentialsPayload("RESET", 5));
  assert(!parseResetCredentialsPayload("", 0));
  assert(!parseResetCredentialsPayload(nullptr, 5));
}

void testBatteryCurves() {
  assert(batteryLevelPercentForChemistry(kBatteryCurveChemistryLiIon, 3.20f) == 0.0f);
  assert(batteryLevelPercentForChemistry(kBatteryCurveChemistryLiIon, 4.16f) == 100.0f);
  assert(std::fabs(batteryLevelPercentForChemistry(kBatteryCurveChemistryLiIon, 3.49f) - 15.0f) < 0.01f);
  assert(std::fabs(batteryLevelPercentForChemistry(kBatteryCurveChemistryLiFePO4, 3.235f) - 35.0f) < 0.01f);
  assert(std::isnan(batteryLevelPercentForChemistry(kBatteryCurveChemistryLiIon, NAN)));
}

void testStatusFormatting() {
  char buffer[160];
  assert(formatDeviceStatus(buffer, sizeof(buffer), nullptr, "", nullptr, nullptr));
  assert(strcmp(buffer, "online") == 0);

  assert(formatDeviceStatus(buffer,
                            sizeof(buffer),
                            "solar_ina226_missing",
                            "battery_ina226_init_failed",
                            nullptr,
                            "bmp3xx_missing"));
  assert(strcmp(buffer,
                "online; degraded: solar_ina226_missing battery_ina226_init_failed bmp3xx_missing") == 0);

  assert(!formatDeviceStatus(buffer, 16, "solar_ina226_missing", nullptr, nullptr, nullptr));
}

void testReadinessFormatting() {
  char buffer[160];
  assert(formatSensorReadiness(buffer,
                               sizeof(buffer),
                               {true, nullptr},
                               {false, "battery_ina226_missing"},
                               {false, nullptr},
                               {true, nullptr}));
  assert(strcmp(buffer, "solar=ready battery=battery_ina226_missing sht4x=unknown bmp3xx=ready") == 0);
}

void testMqttPacketSizing() {
  assert(mqttPacketFits("homeassistant/sensor/device/config", 100, 256, 5));
  assert(!mqttPacketFits("homeassistant/sensor/device/config", 250, 256, 5));
  assert(!mqttPacketFits(nullptr, 1, 256, 5));
}

void testRoutineDiscoveryPolicy() {
  assert(shouldPublishRoutineDiscovery(false, false));
  assert(shouldPublishRoutineDiscovery(false, true));
  assert(shouldPublishRoutineDiscovery(true, false));
  assert(shouldPublishRoutineDiscovery(true, true));
}

void testSerialStyles() {
  assert(strcmp(serialStyleCode(SerialStyle::Success, true), "\033[32m") == 0);
  assert(strcmp(serialStyleCode(SerialStyle::Warning, true), "\033[33m") == 0);
  assert(strcmp(serialStyleCode(SerialStyle::Error, true), "\033[31m") == 0);
  assert(strcmp(serialStyleCode(SerialStyle::Success, false), "") == 0);
  assert(strcmp(serialResetCode(true), "\033[0m") == 0);
  assert(strcmp(serialResetCode(false), "") == 0);
  assert(serialResultStyle(true) == SerialStyle::Success);
  assert(serialResultStyle(false) == SerialStyle::Error);
  assert(serialReadyStyle(true) == SerialStyle::Success);
  assert(serialReadyStyle(false) == SerialStyle::Error);
  assert(strcmp(serialOkFailedText(true, false), "ok") == 0);
  assert(strcmp(serialOkFailedText(false, false), "FAILED") == 0);
  assert(strcmp(serialReadyMissingText(true, false), "ready") == 0);
  assert(strcmp(serialReadyMissingText(false, false), "missing") == 0);
  assert(strcmp(serialYesNoText(true, true), "\033[32myes\033[0m") == 0);
}

void testLongPressButtonLogic() {
  LongPressButtonState state;
  assert(updateLongPressButton(state, false, 0, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 10, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, false, 20, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 100, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 149, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 150, 50, 4000) == LongPressButtonEvent::Pressed);
  assert(updateLongPressButton(state, true, 4149, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 4150, 50, 4000) == LongPressButtonEvent::Triggered);
  assert(updateLongPressButton(state, true, 5000, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, false, 5010, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, false, 5060, 50, 4000) == LongPressButtonEvent::Released);
  assert(updateLongPressButton(state, true, 6000, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 6050, 50, 4000) == LongPressButtonEvent::Pressed);
  assert(updateLongPressButton(state, true, 10050, 50, 4000) == LongPressButtonEvent::Triggered);
}

void testLongPressButtonShortPressDoesNotTrigger() {
  LongPressButtonState state;
  assert(updateLongPressButton(state, false, 0, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 100, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, true, 150, 50, 4000) == LongPressButtonEvent::Pressed);
  assert(updateLongPressButton(state, false, 1000, 50, 4000) == LongPressButtonEvent::None);
  assert(updateLongPressButton(state, false, 1050, 50, 4000) == LongPressButtonEvent::Released);
}

RuntimeConfig makeValidRuntimeConfig() {
  RuntimeConfig config;
  assert(populateRuntimeConfig(config,
                               "192.168.1.10",
                               1883,
                               "",
                               "",
                               "ota-pass",
                               "li_ion",
                               "",
                               "",
                               ""));
  return config;
}

void testRuntimeConfigMqttPortValidation() {
  assert(isValidMqttPort(1));
  assert(isValidMqttPort(1883));
  assert(isValidMqttPort(65535));
  assert(!isValidMqttPort(0));
  assert(!isValidMqttPort(65536));
}

void testRuntimeConfigIpv4Validation() {
  assert(isValidIpv4Address("192.168.1.50"));
  assert(isValidIpv4Address("0.0.0.0"));
  assert(isValidIpv4Address("255.255.255.255"));
  assert(!isValidIpv4Address(""));
  assert(!isValidIpv4Address("192.168.1"));
  assert(!isValidIpv4Address("192.168.1.256"));
  assert(!isValidIpv4Address("192.168.1."));
  assert(!isValidIpv4Address("192.168.one.50"));

  assert(isStaticIpConfigValid("", "", ""));
  assert(isStaticIpConfigValid("192.168.1.50", "192.168.1.1", "255.255.255.0"));
  assert(!isStaticIpConfigValid("", "", kRuntimeConfigDefaultSubnet));
  assert(!isStaticIpConfigValid("192.168.1.50", "", "255.255.255.0"));
  assert(!isStaticIpConfigValid("192.168.1.50", "192.168.1.1", "999.255.255.0"));
}

void testRuntimeConfigBatteryChemistryValidation() {
  uint8_t chemistryId = 255;
  assert(parseBatteryChemistry("li_ion", chemistryId));
  assert(chemistryId == kRuntimeBatteryChemistryLiIon);
  assert(parseBatteryChemistry("LiFePO4", chemistryId));
  assert(chemistryId == kRuntimeBatteryChemistryLiFePO4);
  assert(parseBatteryChemistry("", chemistryId));
  assert(chemistryId == kRuntimeBatteryChemistryLiIon);
  assert(!parseBatteryChemistry("alkaline", chemistryId));
  assert(strcmp(batteryChemistryName(kRuntimeBatteryChemistryLiFePO4), "LiFePO4") == 0);
  assert(strcmp(batteryChemistryKey(kRuntimeBatteryChemistryLiIon), "li_ion") == 0);
}

void testProvisioningIpModeLogic() {
  assert(strcmp(provisioningIpModeForConfig(false), kProvisioningIpModeDhcp) == 0);
  assert(strcmp(provisioningIpModeForConfig(true), kProvisioningIpModeStatic) == 0);
  assert(!provisioningIpModeIsStatic(kProvisioningIpModeDhcp));
  assert(provisioningIpModeIsStatic(kProvisioningIpModeStatic));
  assert(!provisioningIpModeIsStatic(nullptr));
  assert(strcmp(provisioningStaticFieldForDhcpMode(), "") == 0);
  assert(strcmp(provisioningSubnetForStaticMode(""), kRuntimeConfigDefaultSubnet) == 0);
  assert(strcmp(provisioningSubnetForStaticMode(nullptr), kRuntimeConfigDefaultSubnet) == 0);
  assert(strcmp(provisioningSubnetForStaticMode("255.255.0.0"), "255.255.0.0") == 0);
}

void testRuntimeConfigValidation() {
  RuntimeConfig config = makeValidRuntimeConfig();
  assert(validateRuntimeConfig(config).valid);
  assert(config.mqttUsername[0] == '\0');
  assert(config.mqttPassword[0] == '\0');

  config = makeValidRuntimeConfig();
  config.otaPassword[0] = '\0';
  assert(!validateRuntimeConfig(config).valid);

  config = makeValidRuntimeConfig();
  assert(populateRuntimeConfig(config,
                               "mqtt.local",
                               1883,
                               "mqtt-user",
                               "mqtt-pass",
                               "ota-pass",
                               "lifepo4",
                               "192.168.1.50",
                               "192.168.1.1",
                               "255.255.255.0"));
  assert(validateRuntimeConfig(config).valid);
  assert(config.hasStaticIp);
  assert(config.batteryChemistryId == kRuntimeBatteryChemistryLiFePO4);

  config = makeValidRuntimeConfig();
  assert(populateRuntimeConfig(config,
                               "mqtt.local",
                               1883,
                               "mqtt-user",
                               "",
                               "ota-pass",
                               "li_ion",
                               "",
                               "",
                               ""));
  assert(!validateRuntimeConfig(config).valid);

  config = makeValidRuntimeConfig();
  assert(populateRuntimeConfig(config,
                               "mqtt.local",
                               0,
                               "",
                               "",
                               "ota-pass",
                               "li_ion",
                               "",
                               "",
                               ""));
  assert(!validateRuntimeConfig(config).valid);
}

void testRuntimeConfigNormalization() {
  RuntimeConfig config = makeValidRuntimeConfig();
  config.schemaVersion = 0;
  strcpy(config.staticIp, "192.168.1.50");
  strcpy(config.gateway, "192.168.1.1");
  strcpy(config.subnet, "255.255.255.0");
  config.hasStaticIp = false;

  const RuntimeConfig normalized = normalizedRuntimeConfig(config);
  assert(normalized.schemaVersion == kRuntimeConfigSchemaVersion);
  assert(normalized.hasStaticIp);
  assert(validateRuntimeConfig(normalized).valid);
}

void testRuntimeConfigTrimmingAndLengthLimits() {
  RuntimeConfig config;
  assert(populateRuntimeConfig(config,
                               " mqtt.local ",
                               1883,
                               " ",
                               "",
                               " ota-pass ",
                               " life-po4 ",
                               "",
                               "",
                               ""));
  assert(strcmp(config.mqttHost, "mqtt.local") == 0);
  assert(config.mqttUsername[0] == '\0');
  assert(config.mqttPassword[0] == '\0');
  assert(strcmp(config.otaPassword, "ota-pass") == 0);
  assert(config.batteryChemistryId == kRuntimeBatteryChemistryLiFePO4);
  assert(validateRuntimeConfig(config).valid);

  char tooLongHost[kRuntimeConfigMqttHostMaxLength + 2];
  memset(tooLongHost, 'a', sizeof(tooLongHost) - 1);
  tooLongHost[sizeof(tooLongHost) - 1] = '\0';
  assert(!populateRuntimeConfig(config,
                                tooLongHost,
                                1883,
                                "",
                                "",
                                "ota-pass",
                                "li_ion",
                                "",
                                "",
                                ""));
}

}  // namespace

int main() {
  testStayAwakeParsing();
  testResetCredentialsParsing();
  testBatteryCurves();
  testStatusFormatting();
  testReadinessFormatting();
  testMqttPacketSizing();
  testRoutineDiscoveryPolicy();
  testSerialStyles();
  testLongPressButtonLogic();
  testLongPressButtonShortPressDoesNotTrigger();
  testRuntimeConfigMqttPortValidation();
  testRuntimeConfigIpv4Validation();
  testRuntimeConfigBatteryChemistryValidation();
  testProvisioningIpModeLogic();
  testRuntimeConfigValidation();
  testRuntimeConfigNormalization();
  testRuntimeConfigTrimmingAndLengthLimits();
  return 0;
}
