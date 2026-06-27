#include "sensors.h"

#include <Adafruit_BMP3XX.h>
#include <Adafruit_SHT4x.h>
#include <INA226_WE.h>
#include <Wire.h>

#include "battery.h"
#include "config.h"
#include "local_button.h"
#include "runtime_config.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

void waitWithLocalButton(uint32_t durationMs) {
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceLocalButton();
    delay(10);
  }
}

Adafruit_BMP3XX bmp;
Adafruit_SHT4x sht4;
INA226_WE solarIna(kSolarInaAddress);
INA226_WE batteryIna(kBatteryInaAddress);
DeviceState devices;

bool i2cAddressPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void logExpectedDevice(uint8_t address, const char* name, bool present) {
  Serial.printf("I2C %s0x%02X%s %-18s %s\n",
                serialStyle(SerialStyle::Topic),
                address,
                serialReset(),
                name,
                serialPresentMissing(present));
}

void configureBmp390AfterInit() {
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_16X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_32X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_15);
  Serial.println("BMP390/BMP3xx initialized with pressure 32x, temperature 16x, IIR 16x");
  // The BMP3XX first sample after startup may be inaccurate; discard it before publishing.
  if (bmp.performReading()) {
    Serial.printf("BMP390/BMP3xx warm-up discard: temperature %s%.2f C%s, pressure %s%.2f hPa%s\n",
                  serialStyle(SerialStyle::Value),
                  bmp.temperature,
                  serialReset(),
                  serialStyle(SerialStyle::Value),
                  bmp.pressure / 100.0f,
                  serialReset());
  } else {
    Serial.println("BMP390/BMP3xx warm-up discard read failed");
  }
  Serial.printf("Waiting %lu ms after BMP390/BMP3xx warm-up discard\n",
                static_cast<unsigned long>(kBmp390WarmupDiscardDelayMs));
  waitWithLocalButton(kBmp390WarmupDiscardDelayMs);
}

void initializeBmp390() {
  devices.bmp390Ready = false;
  devices.bmp390Issue = nullptr;

  for (uint8_t attempt = 1; attempt <= kBmp390InitAttempts; ++attempt) {
    devices.bmp390Present = i2cAddressPresent(kBmp390Address);
    if (!devices.bmp390Present) {
      Serial.printf("%sBMP390/BMP3xx not present%s at %s0x%02X%s on init attempt %u/%u\n",
                    serialStyle(SerialStyle::Warning),
                    serialReset(),
                    serialStyle(SerialStyle::Topic),
                    kBmp390Address,
                    serialReset(),
                    attempt,
                    kBmp390InitAttempts);
    } else {
      Serial.printf("Initializing BMP390/BMP3xx at 0x%02X, attempt %u/%u\n",
                    kBmp390Address,
                    attempt,
                    kBmp390InitAttempts);
      devices.bmp390Ready = bmp.begin_I2C(kBmp390Address, &Wire);
      if (devices.bmp390Ready) {
        devices.bmp390Issue = nullptr;
        configureBmp390AfterInit();
        return;
      }
      Serial.printf("%sBMP390/BMP3xx initialization failed%s on attempt %u/%u\n",
                    serialStyle(SerialStyle::Error),
                    serialReset(),
                    attempt,
                    kBmp390InitAttempts);
    }

    if (attempt < kBmp390InitAttempts) {
      Serial.printf("Waiting %lu ms before BMP390/BMP3xx init retry\n",
                    static_cast<unsigned long>(kBmp390InitRetryDelayMs));
      waitWithLocalButton(kBmp390InitRetryDelayMs);
    }
  }

  if (!devices.bmp390Present) {
    devices.bmp390Issue = "bmp3xx_missing";
    Serial.printf("%sSkipping BMP390/BMP3xx init%s because %s0x%02X%s is missing after retries\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  serialStyle(SerialStyle::Topic),
                  kBmp390Address,
                  serialReset());
  } else {
    devices.bmp390Issue = "bmp3xx_init_failed";
    Serial.printf("%sBMP390/BMP3xx initialization failed after retries%s\n",
                  serialStyle(SerialStyle::Error),
                  serialReset());
  }
}

void logIna226ReadDiagnostics(INA226_WE& monitor, const char* name) {
  monitor.readAndClearFlags();
  if (monitor.overflow) {
    Serial.printf("%s%s INA226 diagnostic: math overflow flag was set%s\n",
                  serialStyle(SerialStyle::Warning),
                  name,
                  serialReset());
  }
  const uint8_t i2cError = monitor.getI2cErrorCode();
  if (i2cError != 0) {
    Serial.printf("%s%s INA226 diagnostic: I2C error code %u while reading flags%s\n",
                  serialStyle(SerialStyle::Warning),
                  name,
                  i2cError,
                  serialReset());
  }
}

bool valuesAreFinite(float first, float second, float third) {
  return isfinite(first) && isfinite(second) && isfinite(third);
}

bool readIna226Measurements(INA226_WE& monitor,
                            const char* name,
                            float& busVoltageV,
                            float& currentMa,
                            float& powerW) {
  logIna226ReadDiagnostics(monitor, name);

  const float measuredBusVoltageV = monitor.getBusVoltage_V();
  const uint8_t voltageI2cError = monitor.getI2cErrorCode();
  const float measuredCurrentMa = monitor.getCurrent_mA();
  const uint8_t currentI2cError = monitor.getI2cErrorCode();
  const float measuredPowerW = monitor.getBusPower() / 1000.0f;
  const uint8_t powerI2cError = monitor.getI2cErrorCode();

  if (voltageI2cError != 0 || currentI2cError != 0 || powerI2cError != 0) {
    Serial.printf("%s%s INA226 read skipped%s: I2C error codes voltage=%u current=%u power=%u\n",
                  serialStyle(SerialStyle::Warning),
                  name,
                  serialReset(),
                  voltageI2cError,
                  currentI2cError,
                  powerI2cError);
    return false;
  }

  if (!valuesAreFinite(measuredBusVoltageV, measuredCurrentMa, measuredPowerW)) {
    Serial.printf("%s%s INA226 read skipped: invalid measurement value%s\n",
                  serialStyle(SerialStyle::Warning),
                  name,
                  serialReset());
    return false;
  }

  busVoltageV = measuredBusVoltageV;
  currentMa = measuredCurrentMa;
  powerW = measuredPowerW;
  return true;
}

}  // namespace

const DeviceState& deviceState() {
  return devices;
}

void scanI2cBus() {
  logPhase("I2C scan");
  Serial.printf("I2C pins: SDA %sGPIO%u%s, SCL %sGPIO%u%s\n",
                serialStyle(SerialStyle::Topic),
                kI2cSdaPin,
                serialReset(),
                serialStyle(SerialStyle::Topic),
                kI2cSclPin,
                serialReset());
  Serial.println("Expected devices:");
  Serial.printf("  0x%02X INA226 solar monitor\n", kSolarInaAddress);
  Serial.printf("  0x%02X INA226 battery monitor\n", kBatteryInaAddress);
  Serial.printf("  0x%02X SHT41/SHT4x sensor\n", kSht41Address);
  Serial.printf("  0x%02X BMP390/BMP3xx sensor\n", kBmp390Address);
  Serial.println("Scanning I2C bus now");

  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cAddressPresent(address)) {
    Serial.printf("  found %s0x%02X%s\n", serialStyle(SerialStyle::Topic), address, serialReset());
      ++found;
    }
  }
  if (found == 0) {
    Serial.printf("  %sno I2C devices detected%s\n", serialStyle(SerialStyle::Warning), serialReset());
  }

  devices.solarInaPresent = i2cAddressPresent(kSolarInaAddress);
  devices.batteryInaPresent = i2cAddressPresent(kBatteryInaAddress);
  devices.sht41Present = i2cAddressPresent(kSht41Address);
  devices.bmp390Present = i2cAddressPresent(kBmp390Address);

  logExpectedDevice(kSolarInaAddress, "INA226 solar", devices.solarInaPresent);
  logExpectedDevice(kBatteryInaAddress, "INA226 battery", devices.batteryInaPresent);
  logExpectedDevice(kSht41Address, "SHT41/SHT4x", devices.sht41Present);
  logExpectedDevice(kBmp390Address, "BMP390/BMP3xx", devices.bmp390Present);
  Serial.printf(
      "I2C expected-device summary: solar=%s battery=%s sht4x=%s bmp3xx=%s\n",
      serialYesNo(devices.solarInaPresent),
      serialYesNo(devices.batteryInaPresent),
      serialYesNo(devices.sht41Present),
      serialYesNo(devices.bmp390Present));
}

void initializeSensors() {
  logPhase("Sensor initialization");
  initializeBmp390();

  if (devices.sht41Present) {
    Serial.printf("Initializing SHT41/SHT4x at 0x%02X\n", kSht41Address);
    devices.sht41Ready = sht4.begin(&Wire);
    if (devices.sht41Ready) {
      devices.sht41Issue = nullptr;
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sht4.setHeater(SHT4X_NO_HEATER);
      Serial.printf("%sSHT41/SHT4x initialized%s with high precision and heater off\n",
                    serialStyle(SerialStyle::Success),
                    serialReset());
    } else {
      devices.sht41Issue = "sht4x_init_failed";
      Serial.printf("%sSHT41/SHT4x initialization failed%s\n", serialStyle(SerialStyle::Error), serialReset());
    }
  } else {
    devices.sht41Issue = "sht4x_missing";
    Serial.printf("%sSkipping SHT41/SHT4x init%s because %s0x%02X%s is missing\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  serialStyle(SerialStyle::Topic),
                  kSht41Address,
                  serialReset());
  }

  if (devices.solarInaPresent) {
    Serial.printf("Initializing solar INA226 at 0x%02X, shunt %.3f ohm, max current %.2f A\n",
                  kSolarInaAddress,
                  kInaShuntOhms,
                  kSolarMaxCurrentA);
    devices.solarInaReady = solarIna.init();
    if (devices.solarInaReady) {
      devices.solarInaIssue = nullptr;
      solarIna.setResistorRange(kInaShuntOhms, kSolarMaxCurrentA);
      solarIna.waitUntilConversionCompleted();
      Serial.printf("%sSolar INA226 initialized%s\n", serialStyle(SerialStyle::Success), serialReset());
    } else {
      devices.solarInaIssue = "solar_ina226_init_failed";
      Serial.printf("%sSolar INA226 initialization failed%s\n", serialStyle(SerialStyle::Error), serialReset());
    }
  } else {
    devices.solarInaIssue = "solar_ina226_missing";
    Serial.printf("%sSkipping solar INA226 init%s because %s0x%02X%s is missing\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  serialStyle(SerialStyle::Topic),
                  kSolarInaAddress,
                  serialReset());
  }

  if (devices.batteryInaPresent) {
    Serial.printf("Initializing battery INA226 at 0x%02X, shunt %.3f ohm, max current %.2f A\n",
                  kBatteryInaAddress,
                  kInaShuntOhms,
                  kBatteryMaxCurrentA);
    devices.batteryInaReady = batteryIna.init();
    if (devices.batteryInaReady) {
      devices.batteryInaIssue = nullptr;
      batteryIna.setResistorRange(kInaShuntOhms, kBatteryMaxCurrentA);
      batteryIna.waitUntilConversionCompleted();
      Serial.printf("%sBattery INA226 initialized%s\n", serialStyle(SerialStyle::Success), serialReset());
    } else {
      devices.batteryInaIssue = "battery_ina226_init_failed";
      Serial.printf("%sBattery INA226 initialization failed%s\n", serialStyle(SerialStyle::Error), serialReset());
    }
  } else {
    devices.batteryInaIssue = "battery_ina226_missing";
    Serial.printf("%sSkipping battery INA226 init%s because %s0x%02X%s is missing\n",
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  serialStyle(SerialStyle::Topic),
                  kBatteryInaAddress,
                  serialReset());
  }

  Serial.printf(
      "Sensor readiness summary: solar=%s battery=%s sht4x=%s bmp3xx=%s\n",
      serialYesNo(devices.solarInaReady),
      serialYesNo(devices.batteryInaReady),
      serialYesNo(devices.sht41Ready),
      serialYesNo(devices.bmp390Ready));

  Serial.printf("Waiting %lu ms for sensors to settle after initialization\n",
                static_cast<unsigned long>(kSensorPostInitSettleDelayMs));
  waitWithLocalButton(kSensorPostInitSettleDelayMs);
}

Bmp390Reading readBmp390Sensor() {
  Bmp390Reading reading;
  if (devices.bmp390Ready) {
    if (bmp.performReading()) {
      reading.temperatureC = bmp.temperature;
      reading.absolutePressureHpa = bmp.pressure / 100.0f;
      Serial.printf("BMP390/BMP3xx: temperature %s%.2f C%s, pressure %s%.2f hPa%s\n",
                    serialStyle(SerialStyle::Value),
                    reading.temperatureC,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.absolutePressureHpa,
                    serialReset());
    } else {
      Serial.println("BMP390/BMP3xx read failed");
    }
  } else {
    Serial.println("BMP390/BMP3xx skipped: not ready");
  }

  return reading;
}

Sht41Reading readSht41Sensor() {
  Sht41Reading reading;
  if (devices.sht41Ready) {
    sensors_event_t humidity;
    sensors_event_t temperature;
    if (sht4.getEvent(&humidity, &temperature)) {
      reading.temperatureC = temperature.temperature;
      reading.humidityPercent = humidity.relative_humidity;
      Serial.printf("SHT41/SHT4x: temperature %s%.2f C%s, humidity %s%.2f %%%s\n",
                    serialStyle(SerialStyle::Value),
                    reading.temperatureC,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.humidityPercent,
                    serialReset());
    } else {
      Serial.println("SHT41/SHT4x read failed");
    }
  } else {
    Serial.println("SHT41/SHT4x skipped: not ready");
  }

  return reading;
}

Ina226Reading readBatteryIna226() {
  Ina226Reading reading;
  if (devices.batteryInaReady) {
    float busVoltageV = NAN;
    float currentMa = NAN;
    float powerW = NAN;
    if (readIna226Measurements(batteryIna, "Battery", busVoltageV, currentMa, powerW)) {
      // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
      reading.voltageV = busVoltageV;
      reading.currentMa = currentMa;
      reading.powerW = powerW;
      Serial.printf("Battery INA226 (%s): voltage %s%.3f V%s, current %s%.2f mA%s, power %s%.3f W%s, level %s%.1f %%%s\n",
                    batteryChemistryName(runtimeConfig().batteryChemistryId),
                    serialStyle(SerialStyle::Value),
                    reading.voltageV,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.currentMa,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.powerW,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    batteryLevelPercent(reading.voltageV),
                    serialReset());
    }
  } else {
    Serial.println("Battery INA226 skipped: not ready");
  }

  return reading;
}

Ina226Reading readSolarIna226() {
  Ina226Reading reading;
  if (devices.solarInaReady) {
    float busVoltageV = NAN;
    float currentMa = NAN;
    float powerW = NAN;
    if (readIna226Measurements(solarIna, "Solar", busVoltageV, currentMa, powerW)) {
      // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
      reading.voltageV = busVoltageV;
      reading.currentMa = currentMa;
      reading.powerW = powerW;
      Serial.printf("Solar INA226: voltage %s%.3f V%s, current %s%.2f mA%s, power %s%.3f W%s\n",
                    serialStyle(SerialStyle::Value),
                    reading.voltageV,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.currentMa,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    reading.powerW,
                    serialReset());
    }
  } else {
    Serial.println("Solar INA226 skipped: not ready");
  }

  return reading;
}

Reading readSensors() {
  logPhase("Sensor read");
  Reading reading;

  const Bmp390Reading bmp390Reading = readBmp390Sensor();
  reading.bmpTemperatureC = bmp390Reading.temperatureC;
  reading.absolutePressureHpa = bmp390Reading.absolutePressureHpa;

  const Sht41Reading sht41Reading = readSht41Sensor();
  reading.outsideTemperatureC = sht41Reading.temperatureC;
  reading.outsideHumidityPercent = sht41Reading.humidityPercent;

  const Ina226Reading batteryReading = readBatteryIna226();
  reading.batteryVoltageV = batteryReading.voltageV;
  reading.batteryCurrentMa = batteryReading.currentMa;
  reading.batteryPowerW = batteryReading.powerW;
  reading.batteryLevelPercent = batteryLevelPercent(reading.batteryVoltageV);

  const Ina226Reading solarReading = readSolarIna226();
  reading.solarRawVoltageV = solarReading.voltageV;
  reading.solarPanelCurrentMa = solarReading.currentMa;
  reading.solarRawPowerW = solarReading.powerW;

  return reading;
}

}  // namespace Esp32Meteo
