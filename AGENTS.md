# Repository Guidelines

## Project Structure & Module Organization

This is a PlatformIO ESP32 firmware project using the Arduino framework.

- `src/main.cpp` contains the firmware entrypoint, hardware constants, sensor logic, WiFi, MQTT, Home Assistant discovery, deep sleep, and OTA handling.
- `scripts/generate_secrets_header.py` reads local `secrets.yaml` during build and generates `src/secrets_local.h`.
- `HARDWARE_SCHEMATIC.yaml` is the development-time hardware reference only. Do not parse, upload, or embed it at runtime.
- `include/`, `lib/`, and `test/` are standard PlatformIO extension directories.
- `.pio/`, `secrets.yaml`, `src/secrets_local.h`, and `platformio.local.ini` are local/generated and ignored.

## Build, Test, and Development Commands

- `pio run -e esp32dev` builds the USB/esptool firmware environment.
- `pio run -e esp32dev -t upload` uploads over USB, currently `/dev/ttyUSB1`.
- `pio device monitor -b 115200` opens the serial monitor.
- `pio run -e esp32dev_ota` builds the OTA environment.
- `pio run -e esp32dev_ota -t upload` uploads OTA when the ESP32 is awake and reachable.
- If `pio` is not on `PATH`, use `/home/giuseppe/.platformio/penv/bin/pio` with the same arguments.

The build script requires `secrets.yaml` and regenerates `src/secrets_local.h` automatically. Do not commit generated secrets.

## Coding Style & Naming Conventions

Use readable Arduino C++ with two-space indentation, `constexpr` constants, and small helper functions. Keep hardware-derived values explicit near the top of `src/main.cpp`. Use `kCamelCase` for constants, lower camel case for functions and variables, and clear MQTT topic suffixes such as `/sensor/battery_voltage`.

Avoid runtime YAML parsing. Keep Serial logs useful, but never print WiFi, MQTT, OTA passwords, tokens, or full secret values.

## Home Assistant & MQTT Behavior

Home Assistant must keep sensor data available while the ESP32 is in normal deep sleep.

- Publish sensor states retained so Home Assistant and MQTT clients keep the last known values.
- Do not add `availability_topic` or `expire_after` to sensor discovery unless the product decision changes.
- Do not publish an availability `offline` payload for intentional deep sleep.
- Keep `esp32-meteo-v3/status` as a diagnostic text sensor for `online`, `sleeping`, `offline`, `online; degraded: ...`, and `ota_updating`.
- Keep the retained `esp32-meteo-v3/control/stay_awake` switch usable while the ESP32 sleeps.
- Subscribe to `homeassistant/status` and republish retained discovery when Home Assistant announces `online`.
- Retained discovery configs are acceptable. Check discovery payload sizing and log publish success/failure.

## Sensor Handling Notes

- BMP390/BMP3xx first reading after startup can be inaccurate. Discard one warm-up reading before publishing BMP390 temperature or pressure.
- SHT41/SHT4x reads use the Adafruit library's command delay and CRC validation through `getEvent()`; no blanket first-read discard is required unless runtime evidence shows a problem.
- INA226 first conversion can be zero if read too early. Call `waitUntilConversionCompleted()` after initialization/configuration for both INA226 monitors before reading.
- If a sensor read fails or returns `NAN`, skip publishing that state so the retained last known good value remains visible in Home Assistant.

## Testing Guidelines

There is no formal unit test suite yet. Minimum validation is:

- Build both environments with PlatformIO.
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
- Notes about secrets, OTA, or Home Assistant discovery compatibility.

## Security & Configuration Tips

Keep `secrets.yaml`, `src/secrets_local.h`, and `platformio.local.ini` untracked. Store OTA auth in `platformio.local.ini`. Treat `HARDWARE_SCHEMATIC.yaml` as source documentation, not runtime configuration.
