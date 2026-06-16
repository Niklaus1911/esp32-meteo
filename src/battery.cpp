#include "battery.h"

#include <Arduino.h>

#include "battery_curve.h"
#include "config.h"

namespace Esp32Meteo {

float batteryLevelPercent(float voltage) {
  if (isnan(voltage)) {
    return NAN;
  }

  if (BATTERY_CHEMISTRY_ID != kBatteryChemistryLiIon && BATTERY_CHEMISTRY_ID != kBatteryChemistryLiFePO4) {
    Serial.printf("Unknown battery chemistry id %u; using Li-ion curve\n", BATTERY_CHEMISTRY_ID);
  }
  return batteryLevelPercentForChemistry(BATTERY_CHEMISTRY_ID, voltage);
}

}  // namespace Esp32Meteo
