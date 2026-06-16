<h1 align="center">ESP32 Meteo</h1>

<p align="center">
  Battery-powered ESP32 weather-node firmware with MQTT telemetry, Home Assistant discovery,
  solar input monitoring, and deep-sleep operation.
</p>

<p align="center">
  <img alt="CI" src="https://github.com/Niklaus1911/esp32-meteo/actions/workflows/ci.yml/badge.svg">
  <img alt="License" src="https://img.shields.io/github/license/Niklaus1911/esp32-meteo">
  <img alt="PlatformIO" src="https://img.shields.io/badge/PlatformIO-ESP32-orange">
  <img alt="Framework" src="https://img.shields.io/badge/framework-Arduino-00979D">
  <img alt="MQTT" src="https://img.shields.io/badge/MQTT-retained%20telemetry-660066">
  <img alt="Home Assistant" src="https://img.shields.io/badge/Home%20Assistant-discovery-41BDF5">
  <img alt="Power" src="https://img.shields.io/badge/power-deep%20sleep-success">
</p>

## Overview

`esp32-meteo` is PlatformIO/Arduino firmware for ESP32 DevKit and ESP32-C3 DevKitM-1 weather stations. The node wakes, initializes sensors, publishes retained MQTT readings and Home Assistant discovery data, then returns to deep sleep.

The project is built for a sleepy MQTT device:

- Home Assistant keeps the last known sensor values while the ESP32 sleeps.
- MQTT discovery is retained and republished when Home Assistant announces `online`.
- Normal deep sleep is reported as a diagnostic `sleeping` state, not as device unavailability.
- A retained `stay_awake` switch can keep the node online for OTA updates or live testing.

Public source is ready. Public firmware binaries are intentionally not published yet because WiFi, MQTT, OTA, static IP, and battery chemistry settings are currently compiled from local `secrets.yaml`. Do not distribute `.bin` files built with real credentials.

## Features

| Area | Behavior |
| --- | --- |
| Sleep cycle | 10 minute deep-sleep interval by default |
| Sensors | BMP390/BMP3xx, SHT41/SHT4x, solar INA226, battery INA226 |
| Telemetry | Retained MQTT states; invalid or `NAN` readings are skipped |
| Home Assistant | Retained MQTT discovery with stable unique IDs |
| Diagnostics | Reset reason, sensor readiness, WiFi signal, WiFi SSID, IP address, battery chemistry, lifecycle status |
| OTA | ArduinoOTA while the device is awake |
| Power tuning | 80 MHz CPU target and reduced WiFi TX power request |
| Validation | GitHub Actions runs policy checks, Python tests, host C++ tests, and four PlatformIO builds |

## Supported Targets

| PlatformIO environment | Board | Upload mode | MQTT/Home Assistant identity | I2C SDA | I2C SCL |
| --- | --- | --- | --- | --- | --- |
| `esp32dev` | ESP32 DevKit | USB/esptool | `esp32-meteo-v3` | GPIO21 | GPIO22 |
| `esp32dev_ota` | ESP32 DevKit | ArduinoOTA | `esp32-meteo-v3` | GPIO21 | GPIO22 |
| `esp32c3` | ESP32-C3 DevKitM-1 | USB/esptool | `esp32-meteo-c3` | GPIO4 | GPIO5 |
| `esp32c3_ota` | ESP32-C3 DevKitM-1 | ArduinoOTA | `esp32-meteo-c3` | GPIO4 | GPIO5 |

The ESP32 build keeps the existing `esp32-meteo-v3` identity. The ESP32-C3 build uses `esp32-meteo-c3`, so both boards can run on the same MQTT broker and Home Assistant instance.

## Hardware

| Device | Purpose | I2C address |
| --- | --- | --- |
| ESP32 DevKit or ESP32-C3 DevKitM-1 | WiFi, MQTT, OTA, deep sleep, I2C master | n/a |
| BMP390 / BMP3xx | Pressure and BMP temperature | `0x77` |
| SHT41 / SHT4x | Outside temperature and humidity | `0x44` |
| INA226 | Solar input voltage, current, power | `0x40` |
| INA226 | Battery voltage, current, power, level estimate | `0x41` |
| TP5000 module | Single-cell battery charging, configured for installed chemistry | n/a |
| Single-cell battery | Li-ion or LiFePO4 storage | n/a |

Keep I2C pullups tied to 3.3 V only. `HARDWARE_SCHEMATIC.yaml` is a development-time reference for the current build and is not parsed or embedded at runtime.

### Power Design

Firmware deep sleep keeps the ESP32 in a low-power mode between wake cycles, but total battery drain depends heavily on the selected DevKit board, regulator, charger module, and sensor-breakout quiescent current. Measure the assembled hardware before trusting runtime estimates.

Do not power the ESP32 3.3 V rail directly from a single LiFePO4 cell. A fully charged LiFePO4 cell can exceed the ESP32 3.3 V supply range, while a discharged cell may fall too low for stable WiFi operation. Use a low-quiescent-current 3.3 V regulator or buck-boost regulator sized for WiFi transmit current.

Battery percentage is voltage-only. It is useful for rough Li-ion tracking, but LiFePO4 has a flat discharge plateau, so `/sensor/battery_voltage` is the more trustworthy runtime indicator.

## MQTT and Home Assistant

Base topic prefixes:

```text
esp32dev: esp32-meteo-v3
esp32c3:  esp32-meteo-c3
```

Main topic families:

| Topic family | Purpose |
| --- | --- |
| `<prefix>/sensor/...` | Retained sensor readings for temperature, humidity, pressure, battery, and solar input |
| `<prefix>/diagnostic/...` | Retained diagnostics such as reset reason, readiness, WiFi signal, IP, and battery chemistry |
| `<prefix>/status` | Diagnostic lifecycle text: `online`, `sleeping`, `online; degraded: ...`, `ota_updating` |
| `<prefix>/control/stay_awake` | Retained command/state switch for keeping the node awake |
| `homeassistant/.../.../config` | Retained Home Assistant discovery payloads |

The firmware deliberately does not add `availability_topic` or `expire_after` to sensor discovery. A sleeping node should not make Home Assistant mark otherwise valid retained readings as unavailable.

MQTT connection order is intentional:

1. Publish retained `status=online`.
2. Subscribe to retained `stay_awake`.
3. Process the retained stay-awake command.
4. Subscribe to `homeassistant/status`.
5. Publish retained Home Assistant discovery.
6. Publish retained readings and diagnostics.
7. Publish retained `status=sleeping` before deep sleep.

## Configuration

Local configuration is built from `secrets.yaml`, which is intentionally ignored by Git. During PlatformIO builds, `scripts/generate_secrets_header.py` generates `src/secrets_local.h`; that generated header is also ignored.

Start from the tracked example:

```sh
cp secrets.example.yaml secrets.yaml
```

Required values:

| Key | Purpose |
| --- | --- |
| `wifi_primary_ssid` | Primary WiFi network name |
| `wifi_primary_password` | Primary WiFi password |
| `mqtt_host` | MQTT broker hostname or IP address |
| `mqtt_port` | MQTT broker port |
| `mqtt_username` | MQTT username |
| `mqtt_password` | MQTT password |
| `ota_password` | ArduinoOTA password |

Optional values:

| Key | Purpose |
| --- | --- |
| `wifi_backup_ssid`, `wifi_backup_password` | Backup WiFi network; provide both or neither |
| `wifi_static_ip`, `wifi_gateway` | Static IP for ESP32 target; provide both or neither |
| `esp32c3_wifi_static_ip`, `esp32c3_wifi_gateway` | Static IP for ESP32-C3 target; provide both or neither |
| `battery_chemistry` | `li_ion` or `lifepo4`; defaults to `li_ion` |

Use ignored `platformio.local.ini` for local upload overrides such as serial ports, fixed OTA IP addresses, or OTA upload flags. Keep credentials and local network details out of tracked files.

## Build and Upload

Install PlatformIO, create `secrets.yaml`, then build:

```sh
pio run -e esp32dev
pio run -e esp32c3
```

Upload over USB:

```sh
pio run -e esp32dev -t upload
pio run -e esp32c3 -t upload
```

If `pio` is not on `PATH`, use:

```sh
python -m platformio run -e esp32dev
python -m platformio run -e esp32c3
```

## OTA

OTA is available only while the node is awake. Use the retained Home Assistant `Stay Awake` switch before starting OTA, or upload during the normal wake window.

```sh
pio run -e esp32dev_ota
pio run -e esp32dev_ota -t upload
pio run -e esp32c3_ota
pio run -e esp32c3_ota -t upload
```

Default OTA upload hosts:

| Environment | Default host |
| --- | --- |
| `esp32dev_ota` | `esp32-meteo-v3.local` |
| `esp32c3_ota` | `esp32-meteo-c3.local` |

If mDNS does not resolve reliably on your network, override `upload_port` in ignored `platformio.local.ini`.

## Validation and CI

Run the full local quality gate before pushing firmware changes:

```sh
python3 scripts/check_project.py
```

The check verifies:

- required local build inputs exist;
- whitespace is clean;
- local/generated files are not tracked;
- MQTT/Home Assistant behavior guardrails are preserved;
- Python unit tests pass;
- host-side C++ logic tests pass;
- all four PlatformIO environments build;
- ESP32 and ESP32-C3 firmware identity strings are correct.

GitHub Actions runs the same project check on pushes and pull requests. CI uses `secrets.example.yaml` placeholders only, so CI artifacts and logs must not be treated as deployable private firmware.

Manual hardware validation still matters. Check serial logs for I2C scan results, sensor readiness, WiFi connection, MQTT connection, Home Assistant discovery, retained publishes, OTA state, and sleep state. Confirm Home Assistant keeps retained sensor values while the ESP32 is in normal deep sleep.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Build fails with missing secrets | Copy `secrets.example.yaml` to ignored `secrets.yaml` and fill required keys |
| OTA cannot find host | Try the device IP in ignored `platformio.local.ini`; mDNS `.local` support varies by network |
| Home Assistant shows missing values after sleep | Verify MQTT states are retained and discovery has no `availability_topic` or `expire_after` |
| Sensor value is absent | A failed or `NAN` reading is skipped so the last retained good value remains visible |
| Battery runtime is poor | Measure board sleep current; DevKit regulators and sensor breakouts can dominate consumption |
| LiFePO4 percentage looks wrong | Use voltage alongside percentage; LiFePO4 voltage is flat across much of the discharge curve |

## Security and Releases

- Do not commit `secrets.yaml`, `src/secrets_local.h`, `platformio.local.ini`, `.pio/`, or firmware binaries built with real credentials.
- Do not publish public release `.bin` files until the firmware supports no-secrets runtime provisioning.
- OTA, WiFi, and MQTT secrets are currently compile-time values and can be extracted from firmware images by anyone who has the binary.
- The public repository is suitable for source review, local builds, and CI validation.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
