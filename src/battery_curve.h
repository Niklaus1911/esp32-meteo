#pragma once

#include <cstddef>
#include <cstdint>

namespace Esp32Meteo {

constexpr uint8_t kBatteryCurveChemistryLiIon = 0;
constexpr uint8_t kBatteryCurveChemistryLiFePO4 = 1;

struct BatteryCurvePoint {
  float voltage;
  float percent;
};

float batteryLevelPercentForChemistry(uint8_t chemistryId, float voltage);
const BatteryCurvePoint* batteryCurvePointsForChemistry(uint8_t chemistryId, size_t& pointCount);

}  // namespace Esp32Meteo
