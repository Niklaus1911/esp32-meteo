<h1 align="center">ESP32 Meteo</h1>

<p align="center">
  Battery-powered ESP32 weather node with MQTT telemetry, Home Assistant discovery, solar input monitoring, and deep-sleep operation.
</p>

<p align="center">
  <img alt="PlatformIO" src="https://img.shields.io/badge/PlatformIO-ESP32-orange">
  <img alt="Framework" src="https://img.shields.io/badge/framework-Arduino-00979D">
  <img alt="MQTT" src="https://img.shields.io/badge/MQTT-retained%20telemetry-660066">
  <img alt="Home Assistant" src="https://img.shields.io/badge/Home%20Assistant-discovery-41BDF5">
  <img alt="Power" src="https://img.shields.io/badge/power-deep%20sleep-success">
</p>

## Overview

`esp32-meteo` is firmware for an ESP32 DevKit weather station designed to wake, measure, publish retained MQTT states, and return to deep sleep. Home Assistant can discover the device automatically through retained MQTT discovery configs, while retained sensor states remain visible during normal sleep cycles.

The firmware currently targets the `esp32-meteo-v3` MQTT topic prefix and publishes environmental, battery, solar, WiFi, reset, and sensor-readiness diagnostics.

## Highlights

| Area | Behavior |
| --- | --- |
| Power model | 10 minute deep-sleep cycle by default |
| Home Assistant | Retained MQTT discovery, no `availability_topic`, no `expire_after` |
| Sensor states | Retained publishes; invalid or `NAN` readings are skipped |
| Control | Retained `esp32-meteo-v3/control/stay_awake` switch keeps the node awake after the next wake |
| Diagnostics | Retained reset reason, sensor readiness, WiFi signal, WiFi SSID, IP address, and status |
| OTA | ArduinoOTA while the device is awake |

## Hardware

| Device | Purpose | I2C address |
| --- | --- | --- |
| ESP32 DevKit | WiFi, MQTT, OTA, deep sleep, I2C master | n/a |
| BMP390 / BMP3xx | Pressure and BMP temperature | `0x77` |
| SHT41 / SHT4x | Outside temperature and humidity | `0x44` |
| INA226 | Solar input voltage, current, power | `0x40` |
| INA226 | Battery voltage, current, power, level | `0x41` |
| TP5000 module | Single-cell battery charging, configured for installed chemistry | n/a |
| Single-cell battery | Li-ion or LiFePO4 storage, selected in `secrets.yaml` | n/a |

I2C uses GPIO21 for SDA and GPIO22 for SCL. Keep I2C pullups tied to 3.3 V only.

### Power Design

Firmware deep sleep keeps the ESP32 in its low-power sleep mode between wake cycles, but real battery drain depends on the selected DevKit board, regulator, charger module, and sensor breakout quiescent current.

Do not power the ESP32 3.3 V rail directly from a single LiFePO4 cell. A fully charged LiFePO4 cell can exceed the ESP32 3.3 V supply range, while a discharged cell may fall too low for stable WiFi operation. Use a low-quiescent-current 3.3 V regulator or buck-boost regulator sized for WiFi transmit current, then measure the assembled deep-sleep current.

### Battery Chemistry

Set the battery chemistry in local `secrets.yaml`:

```yaml
battery_chemistry: li_ion
```

Valid values are `li_ion` and `lifepo4`. The value defaults to `li_ion` if omitted. The selected chemistry controls the `/sensor/battery_level` voltage-to-percent curve and is published retained at `/diagnostic/battery_chemistry`.

Battery percentage is an estimate from voltage only. It is usually useful for Li-ion cells, but LiFePO4 has a very flat discharge plateau, so the estimate is less precise between roughly 20% and 90%, especially while charging, under WiFi load, or immediately after load changes. Use `/sensor/battery_voltage` alongside the percentage when evaluating real runtime.

## MQTT Topics

Base topic:

```text
esp32-meteo-v3
```

Important topics:

| Topic | Purpose |
| --- | --- |
| `esp32-meteo-v3/status` | Diagnostic lifecycle status: `online`, `sleeping`, `online; degraded: ...`, `ota_updating` |
| `esp32-meteo-v3/control/stay_awake` | Retained Home Assistant switch command and state |
| `esp32-meteo-v3/sensor/bmp390_temperature` | BMP390 temperature |
| `esp32-meteo-v3/sensor/absolute_pressure` | Absolute pressure |
| `esp32-meteo-v3/sensor/outside_temperature` | SHT41 temperature |
| `esp32-meteo-v3/sensor/outside_humidity` | SHT41 humidity |
| `esp32-meteo-v3/sensor/battery_voltage` | Battery voltage |
| `esp32-meteo-v3/sensor/battery_current` | Battery current |
| `esp32-meteo-v3/sensor/battery_power` | Battery power |
| `esp32-meteo-v3/sensor/battery_level` | Estimated battery level |
| `esp32-meteo-v3/sensor/solar_raw_voltage` | Solar input voltage |
| `esp32-meteo-v3/sensor/solar_panel_current` | Solar input current |
| `esp32-meteo-v3/sensor/solar_raw_power` | Solar input power |
| `esp32-meteo-v3/diagnostic/reset_reason` | Last reset reason |
| `esp32-meteo-v3/diagnostic/sensor_readiness` | Compact readiness and degraded sensor state |
| `esp32-meteo-v3/diagnostic/battery_chemistry` | Selected battery chemistry for percentage estimate |

## Home Assistant Behavior

The firmware is intentionally optimized for a sleepy MQTT device:

- Sensor values are published retained so Home Assistant keeps the last known good state while the ESP32 sleeps.
- Sensor discovery configs are published retained under `homeassistant/#`.
- The firmware does not add `availability_topic` or `expire_after` to sensor discovery.
- Intentional deep sleep publishes `status=sleeping`, then disconnects. It does not register a retained MQTT Last Will that can overwrite sleeping with offline.
- `esp32-meteo-v3/status` is a diagnostic text sensor, not a Home Assistant availability source.
- When Home Assistant publishes `online` on `homeassistant/status`, the node republishes retained discovery while awake.

## Setup

1. Install PlatformIO.
2. Copy the example secrets file:

   ```sh
   cp secrets.example.yaml secrets.yaml
   ```

3. Edit `secrets.yaml` with local WiFi, MQTT, OTA, and battery chemistry values.
4. Build the USB upload environment:

   ```sh
   pio run -e esp32dev
   ```

5. Upload over USB:

   ```sh
   pio run -e esp32dev -t upload
   ```

If `pio` is not on `PATH`, use:

```sh
/home/giuseppe/.platformio/penv/bin/pio run -e esp32dev
```

## OTA

Build and upload the OTA environment while the ESP32 is awake and reachable:

```sh
pio run -e esp32dev_ota
pio run -e esp32dev_ota -t upload
```

OTA authentication is loaded from the generated local secrets header and is never printed to Serial.

## Local Configuration

`secrets.yaml` is required at build time and is intentionally ignored by Git. It is parsed by `scripts/generate_secrets_header.py`, which generates `src/secrets_local.h`.

Tracked example:

```text
secrets.example.yaml
```

Ignored local files:

```text
secrets.yaml
src/secrets_local.h
platformio.local.ini
.pio/
```

## Validation

Minimum validation before firmware changes are pushed:

```sh
pio run -e esp32dev
pio run -e esp32dev_ota
```

For hardware validation, check serial logs for I2C scan results, sensor readiness, WiFi, MQTT, Home Assistant discovery, retained publishes, and sleep or OTA state.

## Safety Notes

- Use 3.3 V I2C levels only.
- Do not connect a LiFePO4 cell directly to the ESP32 3.3 V rail; use a suitable low-Iq regulator or buck-boost supply.
- Match the charger module, charge voltage, charge current, and protection circuit to the installed battery chemistry.
- Treat any single-cell Li-ion charger output above about 4.25 V as unsafe or untrusted.
- Verify the TP5000 module variant, chemistry configuration, charge current, regulator path, and battery protection before unattended solar use.
