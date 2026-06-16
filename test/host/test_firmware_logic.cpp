#include <cassert>
#include <cmath>
#include <cstring>

#include "battery_curve.h"
#include "firmware_logic.h"

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

}  // namespace

int main() {
  testStayAwakeParsing();
  testBatteryCurves();
  testStatusFormatting();
  testReadinessFormatting();
  testMqttPacketSizing();
  return 0;
}
