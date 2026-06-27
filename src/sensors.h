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

struct Bmp390Reading {
  float temperatureC = NAN;
  float absolutePressureHpa = NAN;
};

struct Sht41Reading {
  float temperatureC = NAN;
  float humidityPercent = NAN;
};

struct Ina226Reading {
  float voltageV = NAN;
  float currentMa = NAN;
  float powerW = NAN;
};

const DeviceState& deviceState();
void scanI2cBus();
void initializeSensors();
Bmp390Reading readBmp390Sensor();
Sht41Reading readSht41Sensor();
Ina226Reading readBatteryIna226();
Ina226Reading readSolarIna226();
Reading readSensors();

}  // namespace Esp32Meteo
