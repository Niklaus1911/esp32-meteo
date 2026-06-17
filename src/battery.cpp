#include "battery.h"

#include <Arduino.h>

#include "battery_curve.h"
#include "runtime_config.h"

namespace Esp32Meteo {

float batteryLevelPercent(float voltage) {
  if (isnan(voltage)) {
    return NAN;
  }

  const uint8_t chemistryId = runtimeConfig().batteryChemistryId;
  if (!isValidBatteryChemistryId(chemistryId)) {
    Serial.printf("Unknown battery chemistry id %u; using Li-ion curve\n", chemistryId);
  }
  return batteryLevelPercentForChemistry(chemistryId, voltage);
}

}  // namespace Esp32Meteo
