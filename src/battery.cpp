#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace Esp32Meteo {

namespace {

struct BatteryCurvePoint {
  float voltage;
  float percent;
};

struct BatteryCurve {
  const BatteryCurvePoint* points;
  size_t pointCount;
};

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

BatteryCurve activeBatteryCurve() {
  if (BATTERY_CHEMISTRY_ID == kBatteryChemistryLiFePO4) {
    return {kLiFePO4BatteryCurve, sizeof(kLiFePO4BatteryCurve) / sizeof(kLiFePO4BatteryCurve[0])};
  }
  if (BATTERY_CHEMISTRY_ID != kBatteryChemistryLiIon) {
    Serial.printf("Unknown battery chemistry id %u; using Li-ion curve\n", BATTERY_CHEMISTRY_ID);
  }
  return {kLiIonBatteryCurve, sizeof(kLiIonBatteryCurve) / sizeof(kLiIonBatteryCurve[0])};
}

}  // namespace

float batteryLevelPercent(float voltage) {
  if (isnan(voltage)) {
    return NAN;
  }

  const BatteryCurve curve = activeBatteryCurve();
  if (voltage <= curve.points[0].voltage) {
    return 0.0f;
  }

  if (voltage >= curve.points[curve.pointCount - 1].voltage) {
    return 100.0f;
  }

  for (size_t i = 1; i < curve.pointCount; ++i) {
    const BatteryCurvePoint& lower = curve.points[i - 1];
    const BatteryCurvePoint& upper = curve.points[i];
    if (voltage <= upper.voltage) {
      const float ratio = (voltage - lower.voltage) / (upper.voltage - lower.voltage);
      return lower.percent + ratio * (upper.percent - lower.percent);
    }
  }
  return 100.0f;
}

}  // namespace Esp32Meteo
