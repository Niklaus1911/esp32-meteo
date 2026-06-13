# ESP32 Meteo V3 Code Review and Home Assistant Plan

## Summary

The firmware builds successfully for both USB and OTA environments and the core hardware constants match the required ESP32 Meteo V3 design.

Validated constants:

- MQTT topic prefix: `esp32-meteo-v3`
- Stay-awake topic: `esp32-meteo-v3/control/stay_awake`
- I2C pins: SDA `GPIO21`, SCL `GPIO22`
- I2C devices: `0x40`, `0x41`, `0x44`, `0x77`
- Default deep sleep: `600` seconds
- Secrets are generated locally from `secrets.yaml` and are not printed to Serial.

The main issues are Home Assistant integration robustness and MQTT discovery sizing.

## Critical Fixes

### 1. Make Home Assistant Discovery Payloads Safe

Current discovery JSON uses `char payload[1024]`, and MQTT uses a `1024` byte PubSubClient buffer. MQTT packet size includes the topic, payload, and MQTT header, so a payload close to 1024 bytes can still fail.

Fix:

- Increase `mqttClient.setBufferSize()` to at least `2048`.
- Check every discovery `snprintf()` return value.
- If the payload is truncated, log a clear Serial error and skip publishing that discovery config.
- Keep logging the discovery topic and publish result.

### 2. Split Availability From Human Status

Current `esp32-meteo-v3/status` is used for multiple meanings:

- `online`
- `sleeping`
- `offline`
- `online; degraded: ...`
- `ota_updating`

This is useful for humans but not ideal for Home Assistant availability.

Fix:

- Add a dedicated availability topic:
  - `esp32-meteo-v3/availability`
- Publish only Home Assistant availability payloads there:
  - `online`
  - `offline`
- Use this topic as `availability_topic` in every HA discovery entity.
- Keep `esp32-meteo-v3/status` as a diagnostic text sensor for `sleeping`, `degraded`, `ota_updating`, and similar states.

### 3. Improve Home Assistant Device Identity

Current HA device identifier is fixed as `esp32-meteo-v3`. That is fine for one board but will merge multiple flashed ESP32 boards into the same Home Assistant device.

Fix:

- Build a stable MAC string from `ESP.getEfuseMac()`.
- Add a Home Assistant device connection:
  - `"connections":[["mac","AA:BB:CC:DD:EE:FF"]]`
- Prefer using the MAC in MQTT client ID as well.

## Home Assistant Integration Improvements

### Use Better Device Classes

Change BMP390 absolute pressure discovery from:

- `device_class: "pressure"`

to:

- `device_class: "atmospheric_pressure"`

This better matches weather/barometric pressure in Home Assistant.

### Keep Raw MQTT Values

Continue publishing raw sensor values with high precision, for example:

- `esp32-meteo-v3/sensor/bmp390_temperature = 27.11584856`

Home Assistant can handle display rounding, statistics, filtering, and templates.

Optional improvement:

- Add `suggested_display_precision` to HA discovery configs so dashboards look clean while MQTT keeps full raw values.

### Keep Retained Sensor States

For this deep-sleep node, retained sensor state messages are useful because Home Assistant immediately sees the last known values after restart.

Do not use `expire_after` unless intentionally wanting the sleeping node to become unavailable after a timeout.

### Discovery Publishing

Current retained discovery configs are acceptable.

Recommended behavior:

- Publish retained discovery on boot.
- Subscribe to `homeassistant/status`.
- Republish retained discovery when Home Assistant publishes `online`.
- Avoid publishing discovery in tight loops.

## OTA Notes

OTA works only while the ESP32 is awake and connected to WiFi.

Recommended workflow:

1. Publish retained MQTT command:
   - Topic: `esp32-meteo-v3/control/stay_awake`
   - Payload: `true`
   - Retain: enabled
2. Wait for the ESP32 to wake, reconnect, and remain awake.
3. Upload with PlatformIO OTA env:
   - `pio run -e esp32dev_ota -t upload`
4. After OTA, publish retained `false` to allow deep sleep again.

USB upload currently uses:

- Environment: `esp32dev`
- Upload protocol: `esptool`
- Upload port: `/dev/ttyUSB1`

## Validation Checklist

After implementing the fixes:

- `pio run -e esp32dev` succeeds.
- `pio run -e esp32dev_ota` succeeds.
- Serial logs show discovery payload size and publish success/failure.
- Home Assistant device has one grouped device with all sensors and the stay-awake switch.
- HA entities use `availability_topic`.
- HA diagnostic status still reports `sleeping`, `degraded`, and `ota_updating`.
- BMP390 pressure uses `atmospheric_pressure`.
- MQTT raw sensor values keep full precision.
- No WiFi password, MQTT password, OTA password, or tokens are printed.
- `HARDWARE_SCHEMATIC.yaml` is not parsed or embedded at runtime.
