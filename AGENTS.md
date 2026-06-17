# Repository Guidelines

## Project Structure & Module Organization

This is a PlatformIO ESP32 firmware project using the Arduino framework.

- `src/main.cpp` only contains the Arduino `setup()` and `loop()` wrappers.
- `src/app.cpp` owns the firmware lifecycle. Focused modules under `src/` own configuration, sensors, WiFi, MQTT, Home Assistant discovery, telemetry, sleep, OTA, and pure testable logic.
- `src/runtime_config.cpp` owns runtime app configuration validation and ESP32 Preferences/NVS persistence.
- `src/provisioning.cpp` owns WiFiManager captive-portal provisioning for WiFi and runtime app settings.
- `HARDWARE_SCHEMATIC.yaml` is the development-time hardware reference only. Do not parse, upload, or embed it at runtime.
- `include/`, `lib/`, and `test/` are standard PlatformIO extension directories.
- `.pio/` and `platformio.local.ini` are local/generated and ignored.

## Build, Test, and Development Commands

- `pio run -e esp32dev` builds the USB/esptool firmware environment.
- `pio run -e esp32dev -t upload` uploads over USB. Override `upload_port` in ignored `platformio.local.ini` if your board uses a fixed serial device.
- `pio device monitor -b 115200` opens the serial monitor.
- `pio run -e esp32dev_ota` builds the OTA environment.
- `pio run -e esp32dev_ota -t upload` uploads OTA when the ESP32 is awake and reachable.
- If `pio` is not on `PATH`, use `python -m platformio` or your local PlatformIO virtualenv with the same arguments.

Firmware builds do not require local secrets. Runtime WiFi, MQTT, OTA, static IP, and battery chemistry are provisioned on-device and stored in ESP32 NVS.

## Coding Style & Naming Conventions

Use readable Arduino C++ with two-space indentation, `constexpr` constants, and small helper functions. Keep hardware-derived values explicit in `src/config.h`. Use `kCamelCase` for constants, lower camel case for functions and variables, and clear MQTT topic suffixes such as `/sensor/battery_voltage`.

Avoid runtime YAML parsing. Keep Serial logs useful, but never print WiFi, MQTT, OTA passwords, tokens, or full secret values.

## Home Assistant & MQTT Behavior

Home Assistant must keep sensor data available while the ESP32 is in normal deep sleep.

- Publish sensor states retained so Home Assistant and MQTT clients keep the last known values.
- Do not add `availability_topic` or `expire_after` to sensor discovery unless the product decision changes.
- Do not publish an availability `offline` payload for intentional deep sleep.
- Do not register a retained MQTT Last Will on `esp32-meteo-v3/status` that can overwrite intentional `sleeping`.
- Keep `esp32-meteo-v3/status` as a diagnostic text sensor for `online`, `sleeping`, `online; degraded: ...`, and `ota_updating`.
- Keep the retained `esp32-meteo-v3/control/stay_awake` switch usable while the ESP32 sleeps.
- After MQTT connects, publish `status=online`, subscribe to `esp32-meteo-v3/control/stay_awake`, process the retained stay-awake command, then subscribe to `homeassistant/status` and publish retained discovery.
- Subscribe to `homeassistant/status` and republish retained discovery when Home Assistant announces `online`.
- Retained discovery configs are acceptable. Check discovery payload sizing and log publish success/failure.
- Keep retained diagnostic topics for reset reason and compact sensor readiness under `/diagnostic/reset_reason` and `/diagnostic/sensor_readiness`.

## Sensor Handling Notes

- BMP390/BMP3xx first reading after startup can be inaccurate. Discard one warm-up reading before publishing BMP390 temperature or pressure.
- BMP390/BMP3xx init uses bounded retry on cold/battery boot before marking the device degraded; do not block forever.
- SHT41/SHT4x reads use the Adafruit library's command delay and CRC validation through `getEvent()`; no blanket first-read discard is required unless runtime evidence shows a problem.
- INA226 first conversion can be zero if read too early. Call `waitUntilConversionCompleted()` after initialization/configuration for both INA226 monitors before reading.
- Before each INA226 read, call `readAndClearFlags()` and log overflow or non-zero I2C error codes to Serial only.
- If a sensor read fails or returns `NAN`, skip publishing that state so the retained last known good value remains visible in Home Assistant.
- Keep degraded status reasons specific where possible, for example `bmp3xx_missing`, `bmp3xx_init_failed`, `solar_ina226_init_failed`, and `battery_ina226_missing`.

## Testing Guidelines

There is no formal unit test suite yet. Minimum validation is:

- Build both environments with PlatformIO.
- Run `python3 scripts/check_project.py` before PRs; it covers policy checks, Python tests, host C++ logic tests, PlatformIO builds, and firmware identity checks.
- Confirm serial logs show I2C scan, sensor readiness, WiFi, MQTT, HA discovery, publishing, and sleep/OTA state.
- Verify MQTT topics with Home Assistant or an MQTT client.
- Confirm Home Assistant still shows retained sensor values while the ESP32 is in normal deep sleep.
- Confirm deep sleep remains 600 seconds unless stay-awake is enabled.

## Commit & Pull Request Guidelines

Use concise imperative commit messages, for example `Fix MQTT discovery buffer checks` or `Discard initial BMP390 reading`.

Pull requests should include:

- What changed and why.
- Build results for `esp32dev` and `esp32dev_ota`.
- Any serial or MQTT evidence for behavior changes.
- Notes about provisioning, OTA, or Home Assistant discovery compatibility.

## Security & Configuration Tips

Keep `platformio.local.ini` untracked. Do not print WiFi, MQTT, or OTA passwords. Treat `HARDWARE_SCHEMATIC.yaml` as source documentation, not runtime configuration.
