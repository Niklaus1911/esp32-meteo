#include "sensors.h"

#include <Adafruit_BMP3XX.h>
#include <Adafruit_SHT4x.h>
#include <INA226_WE.h>
#include <Wire.h>

#include "battery.h"
#include "config.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

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
  Serial.printf("I2C 0x%02X %-18s %s\n", address, name, present ? "present" : "MISSING");
}

void configureBmp390AfterInit() {
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_16X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_32X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_15);
  Serial.println("BMP390/BMP3xx initialized with pressure 32x, temperature 16x, IIR 16x");
  // The BMP3XX first sample after startup may be inaccurate; discard it before publishing.
  if (bmp.performReading()) {
    Serial.printf("BMP390/BMP3xx warm-up discard: temperature %.2f C, pressure %.2f hPa\n",
                  bmp.temperature,
                  bmp.pressure / 100.0f);
  } else {
    Serial.println("BMP390/BMP3xx warm-up discard read failed");
  }
  Serial.printf("Waiting %lu ms after BMP390/BMP3xx warm-up discard\n",
                static_cast<unsigned long>(kBmp390WarmupDiscardDelayMs));
  delay(kBmp390WarmupDiscardDelayMs);
}

void initializeBmp390() {
  devices.bmp390Ready = false;
  devices.bmp390Issue = nullptr;

  for (uint8_t attempt = 1; attempt <= kBmp390InitAttempts; ++attempt) {
    devices.bmp390Present = i2cAddressPresent(kBmp390Address);
    if (!devices.bmp390Present) {
      Serial.printf("BMP390/BMP3xx not present at 0x%02X on init attempt %u/%u\n",
                    kBmp390Address,
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
      Serial.printf("BMP390/BMP3xx initialization failed on attempt %u/%u\n", attempt, kBmp390InitAttempts);
    }

    if (attempt < kBmp390InitAttempts) {
      Serial.printf("Waiting %lu ms before BMP390/BMP3xx init retry\n",
                    static_cast<unsigned long>(kBmp390InitRetryDelayMs));
      delay(kBmp390InitRetryDelayMs);
    }
  }

  if (!devices.bmp390Present) {
    devices.bmp390Issue = "bmp3xx_missing";
    Serial.printf("Skipping BMP390/BMP3xx init because 0x%02X is missing after retries\n", kBmp390Address);
  } else {
    devices.bmp390Issue = "bmp3xx_init_failed";
    Serial.println("BMP390/BMP3xx initialization failed after retries");
  }
}

void logIna226ReadDiagnostics(INA226_WE& monitor, const char* name) {
  monitor.readAndClearFlags();
  if (monitor.overflow) {
    Serial.printf("%s INA226 diagnostic: math overflow flag was set\n", name);
  }
  const uint8_t i2cError = monitor.getI2cErrorCode();
  if (i2cError != 0) {
    Serial.printf("%s INA226 diagnostic: I2C error code %u while reading flags\n", name, i2cError);
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
    Serial.printf("%s INA226 read skipped: I2C error codes voltage=%u current=%u power=%u\n",
                  name,
                  voltageI2cError,
                  currentI2cError,
                  powerI2cError);
    return false;
  }

  if (!valuesAreFinite(measuredBusVoltageV, measuredCurrentMa, measuredPowerW)) {
    Serial.printf("%s INA226 read skipped: invalid measurement value\n", name);
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
  Serial.printf("I2C pins: SDA GPIO%u, SCL GPIO%u\n", kI2cSdaPin, kI2cSclPin);
  Serial.println("Expected devices:");
  Serial.printf("  0x%02X INA226 solar monitor\n", kSolarInaAddress);
  Serial.printf("  0x%02X INA226 battery monitor\n", kBatteryInaAddress);
  Serial.printf("  0x%02X SHT41/SHT4x sensor\n", kSht41Address);
  Serial.printf("  0x%02X BMP390/BMP3xx sensor\n", kBmp390Address);
  Serial.println("Scanning I2C bus now");

  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cAddressPresent(address)) {
      Serial.printf("  found 0x%02X\n", address);
      ++found;
    }
  }
  if (found == 0) {
    Serial.println("  no I2C devices detected");
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
      yesNo(devices.solarInaPresent),
      yesNo(devices.batteryInaPresent),
      yesNo(devices.sht41Present),
      yesNo(devices.bmp390Present));
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
      Serial.println("SHT41/SHT4x initialized with high precision and heater off");
    } else {
      devices.sht41Issue = "sht4x_init_failed";
      Serial.println("SHT41/SHT4x initialization failed");
    }
  } else {
    devices.sht41Issue = "sht4x_missing";
    Serial.printf("Skipping SHT41/SHT4x init because 0x%02X is missing\n", kSht41Address);
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
      Serial.println("Solar INA226 initialized");
    } else {
      devices.solarInaIssue = "solar_ina226_init_failed";
      Serial.println("Solar INA226 initialization failed");
    }
  } else {
    devices.solarInaIssue = "solar_ina226_missing";
    Serial.printf("Skipping solar INA226 init because 0x%02X is missing\n", kSolarInaAddress);
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
      Serial.println("Battery INA226 initialized");
    } else {
      devices.batteryInaIssue = "battery_ina226_init_failed";
      Serial.println("Battery INA226 initialization failed");
    }
  } else {
    devices.batteryInaIssue = "battery_ina226_missing";
    Serial.printf("Skipping battery INA226 init because 0x%02X is missing\n", kBatteryInaAddress);
  }

  Serial.printf(
      "Sensor readiness summary: solar=%s battery=%s sht4x=%s bmp3xx=%s\n",
      yesNo(devices.solarInaReady),
      yesNo(devices.batteryInaReady),
      yesNo(devices.sht41Ready),
      yesNo(devices.bmp390Ready));

  Serial.printf("Waiting %lu ms for sensors to settle after initialization\n",
                static_cast<unsigned long>(kSensorPostInitSettleDelayMs));
  delay(kSensorPostInitSettleDelayMs);
}

Reading readSensors() {
  logPhase("Sensor read");
  Reading reading;

  if (devices.bmp390Ready) {
    if (bmp.performReading()) {
      reading.bmpTemperatureC = bmp.temperature;
      reading.absolutePressureHpa = bmp.pressure / 100.0f;
      Serial.printf("BMP390/BMP3xx: temperature %.2f C, pressure %.2f hPa\n",
                    reading.bmpTemperatureC,
                    reading.absolutePressureHpa);
    } else {
      Serial.println("BMP390/BMP3xx read failed");
    }
  } else {
    Serial.println("BMP390/BMP3xx skipped: not ready");
  }

  if (devices.sht41Ready) {
    sensors_event_t humidity;
    sensors_event_t temperature;
    if (sht4.getEvent(&humidity, &temperature)) {
      reading.outsideTemperatureC = temperature.temperature;
      reading.outsideHumidityPercent = humidity.relative_humidity;
      Serial.printf("SHT41/SHT4x: temperature %.2f C, humidity %.2f %%\n",
                    reading.outsideTemperatureC,
                    reading.outsideHumidityPercent);
    } else {
      Serial.println("SHT41/SHT4x read failed");
    }
  } else {
    Serial.println("SHT41/SHT4x skipped: not ready");
  }

  if (devices.batteryInaReady) {
    float busVoltageV = NAN;
    float currentMa = NAN;
    float powerW = NAN;
    if (readIna226Measurements(batteryIna, "Battery", busVoltageV, currentMa, powerW)) {
      // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
      reading.batteryVoltageV = busVoltageV;
      reading.batteryCurrentMa = currentMa;
      reading.batteryPowerW = powerW;
      reading.batteryLevelPercent = batteryLevelPercent(reading.batteryVoltageV);
      Serial.printf("Battery INA226 (%s): voltage %.3f V, current %.2f mA, power %.3f W, level %.1f %%\n",
                    BATTERY_CHEMISTRY_NAME,
                    reading.batteryVoltageV,
                    reading.batteryCurrentMa,
                    reading.batteryPowerW,
                    reading.batteryLevelPercent);
    }
  } else {
    Serial.println("Battery INA226 skipped: not ready");
  }

  if (devices.solarInaReady) {
    float busVoltageV = NAN;
    float currentMa = NAN;
    float powerW = NAN;
    if (readIna226Measurements(solarIna, "Solar", busVoltageV, currentMa, powerW)) {
      // Current sign depends on physical INA226 shunt orientation; correct after hardware verification if needed.
      reading.solarRawVoltageV = busVoltageV;
      reading.solarPanelCurrentMa = currentMa;
      reading.solarRawPowerW = powerW;
      Serial.printf("Solar INA226: voltage %.3f V, current %.2f mA, power %.3f W\n",
                    reading.solarRawVoltageV,
                    reading.solarPanelCurrentMa,
                    reading.solarRawPowerW);
    }
  } else {
    Serial.println("Solar INA226 skipped: not ready");
  }

  return reading;
}

}  // namespace Esp32Meteo
