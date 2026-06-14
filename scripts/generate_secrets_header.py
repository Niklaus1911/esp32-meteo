from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SECRETS_FILE = PROJECT_DIR / "secrets.yaml"
HEADER_FILE = PROJECT_DIR / "src" / "secrets_local.h"


def parse_flat_yaml(path):
    values = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in raw_line:
            raise ValueError(f"Invalid secrets.yaml line {line_number}: expected key: value")

        key, value = raw_line.split(":", 1)
        key = key.strip()
        value = value.strip()

        if not key:
            raise ValueError(f"Invalid secrets.yaml line {line_number}: empty key")

        if " #" in value:
            value = value.split(" #", 1)[0].rstrip()

        if (
            len(value) >= 2
            and value[0] == value[-1]
            and value[0] in ("'", '"')
        ):
            value = value[1:-1]

        values[key] = value
    return values


def first_present(values, names, public_name):
    for name in names:
        if name in values and str(values[name]).strip() != "":
            return str(values[name]).strip()
    raise ValueError(
        f"Missing required secret for {public_name}; expected one of: {', '.join(names)}"
    )


def optional_present(values, names):
    for name in names:
        if name in values and str(values[name]).strip() != "":
            return str(values[name]).strip()
    return ""


def first_present_with_legacy(values, canonical_name, legacy_name, public_name):
    canonical_value = optional_present(values, (canonical_name,))
    legacy_value = optional_present(values, (legacy_name,))

    if canonical_value and legacy_value:
        if canonical_value != legacy_value:
            raise ValueError(
                f"{canonical_name} and legacy {legacy_name} are both set but differ; "
                f"keep {canonical_name} as the canonical value or make both values identical"
            )
        return canonical_value

    if canonical_value:
        return canonical_value

    if legacy_value:
        return legacy_value

    raise ValueError(
        f"Missing required secret for {public_name}; expected {canonical_name} "
        f"or legacy {legacy_name}"
    )

def battery_chemistry_config(value):
    normalized = value.strip().lower().replace("-", "_") if value else "li_ion"
    chemistries = {
        "li_ion": (0, "li_ion", "Li-ion"),
        "liion": (0, "li_ion", "Li-ion"),
        "lithium_ion": (0, "li_ion", "Li-ion"),
        "lifepo4": (1, "lifepo4", "LiFePO4"),
        "life_po4": (1, "lifepo4", "LiFePO4"),
    }
    if normalized not in chemistries:
        raise ValueError("battery_chemistry must be one of: li_ion, lifepo4")
    return chemistries[normalized]


def validate_ipv4(value, public_name):
    parts = value.split(".")
    if len(parts) != 4:
        raise ValueError(f"{public_name} must be an IPv4 address")

    for part in parts:
        if not part.isdigit():
            raise ValueError(f"{public_name} must be an IPv4 address")
        number = int(part)
        if number < 0 or number > 255:
            raise ValueError(f"{public_name} must be an IPv4 address")


def cpp_string(value):
    escaped = (
        str(value)
        .replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )
    return f'"{escaped}"'


def generate_header():
    if not SECRETS_FILE.exists():
        raise FileNotFoundError("secrets.yaml is required to generate src/secrets_local.h")

    values = parse_flat_yaml(SECRETS_FILE)

    wifi_primary_ssid = first_present_with_legacy(
        values, "wifi_primary_ssid", "wifi_ssid", "WIFI_PRIMARY_SSID"
    )
    wifi_primary_password = first_present_with_legacy(
        values, "wifi_primary_password", "wifi_password", "WIFI_PASSWORD"
    )
    wifi_backup_ssid = optional_present(values, ("wifi_backup_ssid",))
    wifi_backup_password = optional_present(values, ("wifi_backup_password",))
    wifi_static_ip = optional_present(values, ("wifi_static_ip",))
    wifi_gateway = optional_present(values, ("wifi_gateway",))
    esp32c3_wifi_static_ip = optional_present(values, ("esp32c3_wifi_static_ip",))
    esp32c3_wifi_gateway = optional_present(values, ("esp32c3_wifi_gateway",))

    if bool(wifi_backup_ssid) != bool(wifi_backup_password):
        raise ValueError("wifi_backup_ssid and wifi_backup_password must be provided together")

    if wifi_backup_ssid == wifi_primary_ssid and wifi_backup_password == wifi_primary_password:
        raise ValueError(
            "wifi_backup_ssid and wifi_backup_password match the primary WiFi credentials; "
            "remove the backup keys or configure a real second AP"
        )

    if bool(wifi_static_ip) != bool(wifi_gateway):
        raise ValueError("wifi_static_ip and wifi_gateway must be provided together")

    if bool(esp32c3_wifi_static_ip) != bool(esp32c3_wifi_gateway):
        raise ValueError("esp32c3_wifi_static_ip and esp32c3_wifi_gateway must be provided together")

    if wifi_static_ip:
        validate_ipv4(wifi_static_ip, "wifi_static_ip")
        validate_ipv4(wifi_gateway, "wifi_gateway")

    if esp32c3_wifi_static_ip:
        validate_ipv4(esp32c3_wifi_static_ip, "esp32c3_wifi_static_ip")
        validate_ipv4(esp32c3_wifi_gateway, "esp32c3_wifi_gateway")

    mqtt_host = first_present(values, ("mqtt_host", "mqtt_broker"), "MQTT_HOST")
    mqtt_port_raw = first_present(values, ("mqtt_port",), "MQTT_PORT")
    mqtt_username = first_present(values, ("mqtt_username",), "MQTT_USERNAME")
    mqtt_password = first_present(values, ("mqtt_password",), "MQTT_PASSWORD")
    ota_password = first_present(values, ("ota_password",), "OTA_PASSWORD")
    battery_chemistry_id, battery_chemistry_key, battery_chemistry_name = battery_chemistry_config(
        optional_present(values, ("battery_chemistry",))
    )

    try:
        mqtt_port = int(mqtt_port_raw)
    except ValueError as exc:
        raise ValueError("mqtt_port must be an integer") from exc

    if not 1 <= mqtt_port <= 65535:
        raise ValueError("mqtt_port must be between 1 and 65535")

    HEADER_FILE.parent.mkdir(parents=True, exist_ok=True)
    HEADER_FILE.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                "// Generated by scripts/generate_secrets_header.py from local secrets.yaml.",
                "// Do not commit this file and do not print these values to Serial.",
                f"static constexpr const char* WIFI_PRIMARY_SSID = {cpp_string(wifi_primary_ssid)};",
                f"static constexpr const char* WIFI_PRIMARY_PASSWORD = {cpp_string(wifi_primary_password)};",
                f"static constexpr const char* WIFI_BACKUP_SSID = {cpp_string(wifi_backup_ssid)};",
                f"static constexpr const char* WIFI_BACKUP_PASSWORD = {cpp_string(wifi_backup_password)};",
                f"static constexpr const char* WIFI_STATIC_IP = {cpp_string(wifi_static_ip)};",
                f"static constexpr const char* WIFI_GATEWAY = {cpp_string(wifi_gateway)};",
                f"static constexpr const char* ESP32C3_WIFI_STATIC_IP = {cpp_string(esp32c3_wifi_static_ip)};",
                f"static constexpr const char* ESP32C3_WIFI_GATEWAY = {cpp_string(esp32c3_wifi_gateway)};",
                'static constexpr const char* WIFI_SUBNET = "255.255.255.0";',
                f"static constexpr bool WIFI_HAS_BACKUP = {'true' if wifi_backup_ssid else 'false'};",
                f"static constexpr bool WIFI_HAS_STATIC_IP = {'true' if wifi_static_ip else 'false'};",
                f"static constexpr bool ESP32C3_WIFI_HAS_STATIC_IP = {'true' if esp32c3_wifi_static_ip else 'false'};",
                "static constexpr const char* WIFI_SSID = WIFI_PRIMARY_SSID;",
                "static constexpr const char* WIFI_PASSWORD = WIFI_PRIMARY_PASSWORD;",
                f"static constexpr const char* MQTT_HOST = {cpp_string(mqtt_host)};",
                f"static constexpr uint16_t MQTT_PORT = {mqtt_port};",
                f"static constexpr const char* MQTT_USERNAME = {cpp_string(mqtt_username)};",
                f"static constexpr const char* MQTT_PASSWORD = {cpp_string(mqtt_password)};",
                f"static constexpr const char* OTA_PASSWORD = {cpp_string(ota_password)};",
                f"static constexpr uint8_t BATTERY_CHEMISTRY_ID = {battery_chemistry_id};",
                f"static constexpr const char* BATTERY_CHEMISTRY_KEY = {cpp_string(battery_chemistry_key)};",
                f"static constexpr const char* BATTERY_CHEMISTRY_NAME = {cpp_string(battery_chemistry_name)};",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print("Generated src/secrets_local.h from secrets.yaml")


try:
    generate_header()
except Exception as exc:
    print(f"Error generating src/secrets_local.h: {exc}")
    env.Exit(1)
