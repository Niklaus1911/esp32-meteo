#include "battery.h"

#include <Arduino.h>

#include "battery_curve.h"
#include "runtime_config.h"
#include "util.h"

namespace Esp32Meteo {

float batteryLevelPercent(float voltage) {
  if (isnan(voltage)) {
    return NAN;
  }

  const uint8_t chemistryId = runtimeConfig().batteryChemistryId;
  if (!isValidBatteryChemistryId(chemistryId)) {
    Serial.printf("%sUnknown battery chemistry id %u%s; using Li-ion curve\n",
                  serialStyle(SerialStyle::Warning),
                  chemistryId,
                  serialReset());
  }
  return batteryLevelPercentForChemistry(chemistryId, voltage);
}

}  // namespace Esp32Meteo
