from __future__ import annotations

import textwrap
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from scripts.generate_secrets_header import load_yaml_mapping, render_header


BASE_VALUES = {
    "wifi_primary_ssid": "Primary",
    "wifi_primary_password": "wifi-pass",
    "mqtt_host": "192.168.1.10",
    "mqtt_port": 1883,
    "mqtt_username": "mqtt-user",
    "mqtt_password": "mqtt-pass",
    "ota_password": "ota-pass",
}


class GenerateSecretsHeaderTest(unittest.TestCase):
    def render(self, **updates: object) -> str:
        values = dict(BASE_VALUES)
        values.update(updates)
        return render_header(values)

    def test_renders_required_values(self) -> None:
        header = self.render(battery_chemistry="lifepo4")
        self.assertIn('WIFI_PRIMARY_SSID = "Primary"', header)
        self.assertIn("static constexpr uint16_t MQTT_PORT = 1883;", header)
        self.assertIn("static constexpr uint8_t BATTERY_CHEMISTRY_ID = 1;", header)

    def test_preserves_hash_inside_quoted_yaml_values(self) -> None:
        with TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "secrets.yaml"
            path.write_text(
                textwrap.dedent(
                    """
                    wifi_primary_ssid: "Primary"
                    wifi_primary_password: "abc #123"
                    mqtt_host: "192.168.1.10"
                    mqtt_port: 1883
                    mqtt_username: "mqtt-user"
                    mqtt_password: "mqtt #pass"
                    ota_password: "ota #pass"
                    """
                ),
                encoding="utf-8",
            )

            header = render_header(load_yaml_mapping(path))

        self.assertIn('WIFI_PRIMARY_PASSWORD = "abc #123"', header)
        self.assertIn('MQTT_PASSWORD = "mqtt #pass"', header)
        self.assertIn('OTA_PASSWORD = "ota #pass"', header)

    def test_preserves_yaml_boolean_like_secret_strings(self) -> None:
        with TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "secrets.yaml"
            path.write_text(
                textwrap.dedent(
                    """
                    wifi_primary_ssid: on
                    wifi_primary_password: yes
                    mqtt_host: 192.168.1.10
                    mqtt_port: 1883
                    mqtt_username: no
                    mqtt_password: off
                    ota_password: yes
                    """
                ),
                encoding="utf-8",
            )

            header = render_header(load_yaml_mapping(path))

        self.assertIn('WIFI_PRIMARY_SSID = "on"', header)
        self.assertIn('WIFI_PRIMARY_PASSWORD = "yes"', header)
        self.assertIn('MQTT_USERNAME = "no"', header)
        self.assertIn('MQTT_PASSWORD = "off"', header)
        self.assertIn('OTA_PASSWORD = "yes"', header)

    def test_rejects_mismatched_legacy_wifi_values(self) -> None:
        with self.assertRaisesRegex(ValueError, "legacy wifi_ssid"):
            self.render(wifi_ssid="Legacy")

    def test_rejects_partial_backup_wifi(self) -> None:
        with self.assertRaisesRegex(ValueError, "wifi_backup_ssid and wifi_backup_password"):
            self.render(wifi_backup_ssid="Backup")

    def test_rejects_duplicate_backup_wifi(self) -> None:
        with self.assertRaisesRegex(ValueError, "match the primary WiFi credentials"):
            self.render(wifi_backup_ssid="Primary", wifi_backup_password="wifi-pass")

    def test_rejects_invalid_static_ip(self) -> None:
        with self.assertRaisesRegex(ValueError, "wifi_static_ip must be an IPv4 address"):
            self.render(wifi_static_ip="192.168.1.999", wifi_gateway="192.168.1.1")

    def test_rejects_invalid_mqtt_port(self) -> None:
        with self.assertRaisesRegex(ValueError, "mqtt_port must be between"):
            self.render(mqtt_port=70000)

    def test_rejects_invalid_battery_chemistry(self) -> None:
        with self.assertRaisesRegex(ValueError, "battery_chemistry"):
            self.render(battery_chemistry="alkaline")


if __name__ == "__main__":
    unittest.main()
