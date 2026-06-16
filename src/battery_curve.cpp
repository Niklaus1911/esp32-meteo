#include "battery_curve.h"

#include <cmath>

namespace Esp32Meteo {

namespace {

constexpr BatteryCurvePoint kLiIonBatteryCurve[] = {
    {3.20f, 0.0f},   {3.40f, 10.0f},  {3.58f, 20.0f},
    {3.68f, 30.0f},  {3.74f, 40.0f},  {3.79f, 50.0f},
    {3.85f, 60.0f},  {3.92f, 70.0f},  {4.00f, 80.0f},
    {4.10f, 90.0f},  {4.16f, 100.0f},
};

constexpr BatteryCurvePoint kLiFePO4BatteryCurve[] = {
    {2.80f, 0.0f},   {3.00f, 5.0f},   {3.10f, 10.0f},
    {3.18f, 20.0f},  {3.22f, 30.0f},  {3.25f, 40.0f},
    {3.27f, 50.0f},  {3.28f, 60.0f},  {3.30f, 70.0f},
    {3.32f, 80.0f},  {3.35f, 90.0f},  {3.45f, 100.0f},
};

}  // namespace

const BatteryCurvePoint* batteryCurvePointsForChemistry(uint8_t chemistryId, size_t& pointCount) {
  if (chemistryId == kBatteryCurveChemistryLiFePO4) {
    pointCount = sizeof(kLiFePO4BatteryCurve) / sizeof(kLiFePO4BatteryCurve[0]);
    return kLiFePO4BatteryCurve;
  }

  pointCount = sizeof(kLiIonBatteryCurve) / sizeof(kLiIonBatteryCurve[0]);
  return kLiIonBatteryCurve;
}

float batteryLevelPercentForChemistry(uint8_t chemistryId, float voltage) {
  if (std::isnan(voltage)) {
    return NAN;
  }

  size_t pointCount = 0;
  const BatteryCurvePoint* points = batteryCurvePointsForChemistry(chemistryId, pointCount);

  if (voltage <= points[0].voltage) {
    return 0.0f;
  }

  if (voltage >= points[pointCount - 1].voltage) {
    return 100.0f;
  }

  for (size_t i = 1; i < pointCount; ++i) {
    const BatteryCurvePoint& lower = points[i - 1];
    const BatteryCurvePoint& upper = points[i];
    if (voltage <= upper.voltage) {
      const float ratio = (voltage - lower.voltage) / (upper.voltage - lower.voltage);
      return lower.percent + ratio * (upper.percent - lower.percent);
    }
  }

  return 100.0f;
}

}  // namespace Esp32Meteo
