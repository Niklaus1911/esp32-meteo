#pragma once

#include <Arduino.h>

namespace Esp32Meteo {

struct DeviceState {
  bool solarInaPresent = false;
  bool batteryInaPresent = false;
  bool sht41Present = false;
  bool bmp390Present = false;
  bool solarInaReady = false;
  bool batteryInaReady = false;
  bool sht41Ready = false;
  bool bmp390Ready = false;
  const char* solarInaIssue = nullptr;
  const char* batteryInaIssue = nullptr;
  const char* sht41Issue = nullptr;
  const char* bmp390Issue = nullptr;
};

struct Reading {
  float bmpTemperatureC = NAN;
  float absolutePressureHpa = NAN;
  float outsideTemperatureC = NAN;
  float outsideHumidityPercent = NAN;
  float batteryVoltageV = NAN;
  float batteryCurrentMa = NAN;
  float batteryPowerW = NAN;
  float batteryLevelPercent = NAN;
  float solarRawVoltageV = NAN;
  float solarPanelCurrentMa = NAN;
  float solarRawPowerW = NAN;
};

const DeviceState& deviceState();
void scanI2cBus();
void initializeSensors();
Reading readSensors();

}  // namespace Esp32Meteo
