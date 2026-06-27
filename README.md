<h1 align="center">ESP32 Meteo</h1>

<p align="center">
  Battery-powered ESP32 weather-node firmware with MQTT telemetry, Home Assistant discovery,
  solar input monitoring, and deep-sleep operation.
</p>

<p align="center">
  <img alt="CI" src="https://github.com/Niklaus1911/esp32-meteo/actions/workflows/ci.yml/badge.svg">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-blue">
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

The firmware no longer embeds WiFi, MQTT, OTA, static IP, or battery chemistry secrets in the binary. First boot uses a WiFiManager captive portal, and app settings are stored on the device in ESP32 NVS.

## Features

| Area | Behavior |
| --- | --- |
| Sleep cycle | 10 minute deep-sleep interval by default |
| Sensors | BMP390/BMP3xx, SHT41/SHT4x, solar INA226, battery INA226 |
| Telemetry | Retained MQTT states published by staggered sensor group; invalid or `NAN` readings are skipped |
| Home Assistant | Retained MQTT discovery with stable unique IDs; discovery runs during MQTT setup before first telemetry |
| Diagnostics | Reset reason, sensor readiness, WiFi signal, WiFi SSID, IP address, battery chemistry, lifecycle status, boot phase |
| Recovery | Home Assistant button or 4-second BOOT-button hold for clearing saved credentials and reopening the setup portal |
| OTA | ArduinoOTA while the device is awake |
| Power tuning | 80 MHz CPU target and reduced WiFi TX power request |
| Validation | GitHub Actions runs policy checks, Python tests, host C++ tests, and four PlatformIO builds |

Firmware uses the Arduino ESP32 `min_spiffs.csv` partition table. This keeps two OTA-capable app slots and gives each slot more room than the default 4 MB layout. SPIFFS is intentionally minimized because the firmware does not use a flash filesystem.

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
| `<prefix>/status` | Diagnostic lifecycle text: `online`, `sleeping`, `online; degraded: ...`, `ota_updating`, `resetting_credentials` |
| `<prefix>/control/stay_awake` | Retained command/state switch for keeping the node awake |
| `<prefix>/control/reset_credentials` | Non-retained command topic used by the Home Assistant `Reset Credentials` button |
| `homeassistant/.../.../config` | Retained Home Assistant discovery payloads |

The firmware deliberately does not add `availability_topic` or `expire_after` to sensor discovery. A sleeping node should not make Home Assistant mark otherwise valid retained readings as unavailable.

MQTT connection order is intentional:

1. Publish retained `status=online`.
2. Subscribe to retained `stay_awake`.
3. Process the retained stay-awake command.
4. Subscribe to `homeassistant/status`.
5. Clear any stale retained `reset_credentials` command and subscribe to the reset command topic.
6. Publish retained Home Assistant discovery.
7. Publish retained readings and diagnostics one sensor group at a time.
8. Publish retained `status=sleeping` before deep sleep.

Home Assistant also discovers a `Reset Credentials` button. Pressing it publishes the exact payload `reset` to `<prefix>/control/reset_credentials` while the ESP32 is awake and not updating OTA. The same reset can be requested locally by holding the board BOOT button for 4 seconds while the firmware is awake. The firmware then publishes retained `status=resetting_credentials` when MQTT is available, clears saved runtime app config, erases saved WiFi station credentials, and reboots. The next boot starts the same WiFiManager setup portal used on first flash. This is a local device reset only; retained Home Assistant discovery topics and retained sensor states on the MQTT broker are not deleted.

## Configuration

Runtime setup is done on the device. On first boot, or whenever the saved runtime app config is missing or invalid, the node starts a WiFiManager setup AP named `ESP32-Meteo-Setup-<chipid>` with no timeout. Connect to that AP and fill in WiFi plus the runtime fields below. The ESP32 stays awake in setup mode until valid settings are saved or power is removed, so first-boot provisioning can consume significant battery power.

After valid settings are saved, the ESP32 stores them and reboots. The next boot uses the saved WiFi and runtime settings through the normal startup path.

This is the first runtime-provisioned release of the project. There is no legacy compile-time-secret migration path because no production devices were already flashed with the old secrets-based firmware.

Required runtime fields:

| Key | Purpose |
| --- | --- |
| `mqtt_host` | MQTT broker hostname or IP address |
| `mqtt_port` | MQTT broker port |
| `ota_password` | ArduinoOTA password |

Optional runtime fields:

| Key | Purpose |
| --- | --- |
| `mqtt_username` | MQTT username; leave both username and password empty for anonymous MQTT |
| `mqtt_password` | MQTT password; leave both username and password empty for anonymous MQTT |
| `ip_mode` | `DHCP` or `Static`; DHCP saves empty static IP fields |
| `static_ip`, `gateway`, `subnet` | Static WiFi config; in Static mode provide IP and gateway. Subnet is prefilled as `255.255.255.0` and uses that value if left empty |
| `battery_chemistry` | Portal dropdown for `Li-ion` or `LiFePO4`; defaults to `Li-ion` |

Saved WiFi credentials are managed by the ESP32 WiFi stack through WiFiManager. Saved MQTT, OTA, static IP, and battery chemistry settings are stored in ESP32 Preferences/NVS under the firmware namespace.

Normal boots with saved config connect directly to the saved WiFi network. If WiFi is unavailable, the device sleeps instead of repeatedly opening the setup portal, which protects battery runtime. To intentionally reprovision an awake device, press the Home Assistant `Reset Credentials` button or hold BOOT for 4 seconds during a wake window. The BOOT-button path is useful when the device can no longer reach MQTT.

Use ignored `platformio.local.ini` for local upload overrides such as serial ports, fixed OTA IP addresses, or OTA upload flags. Keep local network details out of tracked files.

## Build and Upload

Install PlatformIO, then build:

```sh
pio run -e esp32dev
pio run -e esp32c3
```

Upload over USB:

```sh
pio run -e esp32dev -t upload
pio run -e esp32c3 -t upload
```

Flash once over USB after changing from an older partition table. OTA uploads can update firmware later, but they do not rewrite the partition table.

If `pio` is not on `PATH`, use:

```sh
python -m platformio run -e esp32dev
python -m platformio run -e esp32c3
```

## Serial Logs

Serial output uses ANSI colors by default to make phases, successful actions, warnings, errors, MQTT topics, and measured values easier to scan. The project enables PlatformIO raw monitor mode so ANSI escape sequences pass through to the terminal instead of being printed as visible `␛` characters.

The equivalent manual monitor command is:

```sh
pio device monitor -b 115200 --raw
```

If your serial monitor prints escape sequences literally, disable colored logs with the build flag:

```ini
-DESP32_METEO_SERIAL_ANSI_COLORS=0
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

- whitespace is clean;
- local/generated files are not tracked;
- MQTT/Home Assistant behavior guardrails are preserved;
- Python unit tests pass;
- host-side C++ logic tests pass;
- all four PlatformIO environments build;
- ESP32 and ESP32-C3 firmware identity strings are correct.

GitHub Actions runs the same project check on pushes and pull requests. CI builds do not require local secrets.

Manual hardware validation still matters. Check serial logs and `<prefix>/diagnostic/boot_phase` for I2C scan results, sensor readiness, WiFi connection, MQTT connection, Home Assistant discovery, staggered retained publishes, OTA state, and sleep state. Pre-telemetry boot-phase diagnostics are Serial-only; retained Home Assistant discovery intentionally runs before first telemetry so new devices are discoverable. Confirm Home Assistant keeps retained sensor values while the ESP32 is in normal deep sleep.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| First boot has no normal WiFi | Connect to the `ESP32-Meteo-Setup-<chipid>` AP and complete provisioning; the setup AP has no timeout |
| Provisioning rejects static IP settings | In Static mode, fill a valid `static_ip` and `gateway`; subnet defaults to `255.255.255.0` if empty |
| Wrong WiFi or app config was saved | Press Home Assistant `Reset Credentials` while MQTT is reachable, or hold BOOT for 4 seconds during a wake window |
| `Reset Credentials` does nothing | The MQTT button requires the ESP32 to be awake, connected to MQTT, and not in an OTA update; the BOOT-button reset requires the firmware to be awake and not in an OTA update |
| OTA cannot find host | Try the device IP in ignored `platformio.local.ini`; mDNS `.local` support varies by network |
| Home Assistant shows missing values after sleep | Verify MQTT states are retained and discovery has no `availability_topic` or `expire_after` |
| Sensor value is absent | A failed or `NAN` reading is skipped so the last retained good value remains visible |
| Battery runtime is poor | Measure board sleep current; DevKit regulators and sensor breakouts can dominate consumption |
| LiFePO4 percentage looks wrong | Use voltage alongside percentage; LiFePO4 voltage is flat across much of the discharge curve |

## Security and Releases

- Do not commit `platformio.local.ini`, `.pio/`, or local firmware artifacts you do not intend to publish.
- Public firmware binaries should not contain private WiFi, MQTT, or OTA strings; these values are provisioned at runtime.
- Runtime config in NVS keeps secrets out of firmware binaries, but it is not physical secret protection unless flash encryption and related ESP32 security features are enabled.
- The public repository is suitable for source review, local builds, CI validation, and no-secrets firmware builds.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
