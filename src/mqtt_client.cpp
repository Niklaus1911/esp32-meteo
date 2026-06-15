#include "mqtt_client.h"

#include <WiFi.h>

#include "config.h"
#include "ha_discovery.h"
#include "ota_service.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

WiFiClient wifiClient;
PubSubClient mqttClientInstance(wifiClient);

bool parseStayAwakePayload(const char* payload, size_t length, bool& value) {
  String normalized;
  normalized.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    if (!isspace(static_cast<unsigned char>(payload[i]))) {
      normalized += static_cast<char>(tolower(static_cast<unsigned char>(payload[i])));
    }
  }

  if (normalized == "true" || normalized == "on" || normalized == "1" || normalized == "yes") {
    value = true;
    return true;
  }
  if (normalized == "false" || normalized == "off" || normalized == "0" || normalized == "no") {
    value = false;
    return true;
  }
  return false;
}

void mqttCallback(char* receivedTopic, byte* payload, unsigned int length) {
  if (strcmp(receivedTopic, kHaStatusTopic) == 0) {
    bool isHomeAssistantOnline = length == 6;
    const char expected[] = "online";
    for (unsigned int i = 0; isHomeAssistantOnline && i < length; ++i) {
      isHomeAssistantOnline = payload[i] == expected[i];
    }

    if (isHomeAssistantOnline) {
      Serial.println("Home Assistant announced online; discovery republish queued");
      homeAssistantDiscoveryRequested = true;
    }
    return;
  }

  if (strcmp(receivedTopic, kStatusTopic) == 0) {
    if (statusConfirmationPending && payloadEquals(payload, length, statusConfirmationExpected)) {
      statusConfirmationReceived = true;
      Serial.printf("MQTT status confirmation received on %s: %s\n",
                    kStatusTopic,
                    statusConfirmationExpected);
    } else if (statusConfirmationPending) {
      Serial.printf("MQTT status confirmation ignored on %s: payload length=%u, expected=%s\n",
                    kStatusTopic,
                    length,
                    statusConfirmationExpected ? statusConfirmationExpected : "(none)");
    }
    return;
  }

  if (strcmp(receivedTopic, kStayAwakeTopic) != 0) {
    return;
  }

  bool parsedValue = false;
  if (parseStayAwakePayload(reinterpret_cast<const char*>(payload), length, parsedValue)) {
    stayAwakeRequested = parsedValue;
    stayAwakeCommandReceived = true;
    Serial.printf("Stay-awake command received on %s: %s\n",
                  kStayAwakeTopic,
                  stayAwakeRequested ? "true" : "false");
  } else {
    Serial.printf("Ignoring invalid stay-awake command payload on %s, length=%u\n", kStayAwakeTopic, length);
  }
}

bool publishRetainedStatusAndWaitForEcho(const char* payload, uint32_t timeoutMs) {
  statusConfirmationExpected = payload;
  statusConfirmationReceived = false;
  statusConfirmationPending = true;

  const bool statusOk = mqttClientInstance.publish(kStatusTopic, payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n",
                kStatusTopic,
                payload,
                statusOk ? "ok" : "FAILED");
  if (!statusOk) {
    statusConfirmationPending = false;
    statusConfirmationExpected = nullptr;
    return false;
  }

  if (!statusConfirmationSubscribed) {
    const bool subscribed = mqttClientInstance.subscribe(kStatusTopic, 0);
    Serial.printf("MQTT subscribe %s qos=0 for status confirmation: %s\n",
                  kStatusTopic,
                  subscribed ? "ok" : "FAILED");
    statusConfirmationSubscribed = subscribed;
    if (!subscribed) {
      statusConfirmationPending = false;
      statusConfirmationExpected = nullptr;
      return false;
    }
  }

  Serial.printf("Waiting up to %lu ms for broker echo of %s = %s\n",
                static_cast<unsigned long>(timeoutMs),
                kStatusTopic,
                payload);
  const uint32_t started = millis();
  while (!statusConfirmationReceived && mqttClientInstance.connected() && millis() - started < timeoutMs) {
    serviceMqttAndOta();
    delay(10);
  }

  const bool confirmed = statusConfirmationReceived;
  statusConfirmationPending = false;
  statusConfirmationExpected = nullptr;
  Serial.printf("MQTT broker echo for %s = %s: %s\n",
                kStatusTopic,
                payload,
                confirmed ? "confirmed" : "not confirmed");
  return confirmed;
}

}  // namespace

PubSubClient& mqttClient() {
  return mqttClientInstance;
}

void serviceMqttAndOta() {
  handleOta();
  mqttClientInstance.loop();
}

void waitWithMqttAndOta(uint32_t durationMs, const char* description) {
  if (durationMs == 0) {
    return;
  }

  Serial.printf("%s for %lu ms\n", description, static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceMqttAndOta();
    delay(10);
  }
  Serial.printf("%s complete\n", description);
}

bool connectMqtt() {
  logPhase("MQTT");
  mqttClientInstance.setServer(MQTT_HOST, MQTT_PORT);
  mqttClientInstance.setCallback(mqttCallback);
  const bool bufferConfigured = mqttClientInstance.setBufferSize(kMqttBufferSize);

  Serial.printf("MQTT server: %s:%u\n", MQTT_HOST, MQTT_PORT);
  Serial.printf("MQTT buffer size %u bytes: %s\n",
                static_cast<unsigned int>(kMqttBufferSize),
                bufferConfigured ? "ok" : "FAILED");
  Serial.printf("MQTT client ID prefix: %s\n", kTopicPrefix);
  Serial.printf("MQTT status topic: %s\n", kStatusTopic);
  Serial.printf("MQTT stay-awake topic: %s\n", kStayAwakeTopic);
  if (!bufferConfigured) {
    return false;
  }
  Serial.printf("Connecting to MQTT with timeout %lu ms\n", static_cast<unsigned long>(kMqttConnectTimeoutMs));
  const uint32_t started = millis();
  uint8_t attempt = 0;
  while (!mqttClientInstance.connected() && millis() - started < kMqttConnectTimeoutMs) {
    ++attempt;
    const String clientId = String(kTopicPrefix) + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    Serial.printf("MQTT connect attempt %u using client ID %s\n", attempt, clientId.c_str());
    if (mqttClientInstance.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.printf("MQTT connected in %lu ms\n", static_cast<unsigned long>(millis() - started));
      const bool statusPublished = mqttClientInstance.publish(kStatusTopic, "online", true);
      Serial.printf("MQTT publish %s = online retained: %s\n", kStatusTopic, statusPublished ? "ok" : "FAILED");
      const bool subscribed = mqttClientInstance.subscribe(kStayAwakeTopic, 1);
      Serial.printf("MQTT subscribe %s qos=1: %s\n", kStayAwakeTopic, subscribed ? "ok" : "FAILED");
      if (!subscribed) {
        return false;
      }
      waitForRetainedStayAwakeCommand();
      const bool haSubscribed = mqttClientInstance.subscribe(kHaStatusTopic, 0);
      Serial.printf("MQTT subscribe %s qos=0: %s\n", kHaStatusTopic, haSubscribed ? "ok" : "FAILED");
      if (!haSubscribed) {
        return false;
      }
      publishHomeAssistantDiscovery();
      return true;
    }

    Serial.printf("MQTT connect failed, rc=%d\n", mqttClientInstance.state());
    delay(kMqttRetryDelayMs);
  }

  Serial.println("MQTT connection failed");
  return false;
}

void publishFloat(const char* suffix, float value) {
  const String fullTopic = topic(suffix);
  if (isnan(value) || isinf(value)) {
    Serial.printf("MQTT skip %s: invalid reading\n", fullTopic.c_str());
    return;
  }

  char payload[24];
  dtostrf(value, 0, 8, payload);
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", fullTopic.c_str(), payload, ok ? "ok" : "FAILED");
}

void publishText(const char* suffix, const char* value, bool retained) {
  const String fullTopic = topic(suffix);
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), value, retained);
  Serial.printf("MQTT publish %s = %s retained=%s: %s\n",
                fullTopic.c_str(),
                value,
                retained ? "true" : "false",
                ok ? "ok" : "FAILED");
}

void publishDiscoveryPayload(const char* component, const char* objectId, const char* payload) {
  char discoveryTopic[160];
  if (!formatInto(discoveryTopic,
                  sizeof(discoveryTopic),
                  "Home Assistant discovery topic",
                  "%s/%s/%s/config",
                  kHaDiscoveryPrefix,
                  component,
                  objectId)) {
    Serial.printf("HA discovery skipped for %s/%s: topic too long\n", component, objectId);
    return;
  }

  const size_t payloadLength = strlen(payload);
  const size_t packetBytes = kMqttMaxHeaderBytes + 2 + strlen(discoveryTopic) + payloadLength;
  Serial.printf("HA discovery payload %s: payload=%u bytes, packet_estimate=%u/%u bytes\n",
                discoveryTopic,
                static_cast<unsigned int>(payloadLength),
                static_cast<unsigned int>(packetBytes),
                static_cast<unsigned int>(kMqttBufferSize));
  if (packetBytes > kMqttBufferSize) {
    Serial.printf("HA discovery skipped %s: MQTT packet estimate exceeds buffer\n", discoveryTopic);
    return;
  }

  const bool ok = mqttClientInstance.publish(discoveryTopic, payload, true);
  Serial.printf("HA discovery publish %s retained: %s\n", discoveryTopic, ok ? "ok" : "FAILED");
}

void flushMqtt(uint32_t durationMs) {
  Serial.printf("Flushing MQTT for %lu ms\n", static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    serviceMqttAndOta();
    delay(10);
  }
  Serial.println("MQTT flush complete");
}

bool publishSleepStatusWithConfirmation() {
  for (uint8_t attempt = 1; attempt <= kSleepStatusConfirmAttempts; ++attempt) {
    Serial.printf("Publishing sleeping status with broker confirmation, attempt %u/%u\n",
                  attempt,
                  kSleepStatusConfirmAttempts);
    if (publishRetainedStatusAndWaitForEcho("sleeping", kSleepStatusConfirmTimeoutMs)) {
      return true;
    }

    if (attempt < kSleepStatusConfirmAttempts) {
      waitWithMqttAndOta(kMqttRetryDelayMs, "Sleeping status confirmation retry wait");
    }
  }

  Serial.println("WARNING: MQTT broker did not confirm retained sleeping status; sleeping anyway to protect battery");
  return false;
}

void waitForRetainedStayAwakeCommand() {
  logPhase("Stay-awake command");
  Serial.println("MQTT commands cannot wake the ESP32 while it is already in deep sleep.");
  Serial.println("A retained stay-awake command will be applied after the next timer wake and MQTT reconnect.");

  if (kForceStayAwakeForTesting) {
    stayAwakeRequested = true;
    stayAwakeCommandReceived = true;
    Serial.println("Local force stay-awake flag is enabled; bypassing retained MQTT command wait");
    return;
  }

  Serial.printf("Waiting up to %lu ms for retained command on %s\n",
                static_cast<unsigned long>(kRetainedCommandWaitMs),
                kStayAwakeTopic);
  const uint32_t started = millis();
  while (!stayAwakeCommandReceived && millis() - started < kRetainedCommandWaitMs) {
    serviceMqttAndOta();
    delay(10);
  }

  if (!stayAwakeCommandReceived) {
    Serial.println("No retained stay-awake command received before timeout; defaulting to one publish then sleep");
  } else {
    Serial.printf("Retained stay-awake command applied: %s\n", stayAwakeRequested ? "true" : "false");
    Serial.printf("Stay-awake retained value means this boot will %s\n",
                  stayAwakeRequested ? "remain awake" : "return to deep sleep after publishing");
  }

  if (homeAssistantDiscoveryRequested) {
    publishHomeAssistantDiscovery();
  }
}

}  // namespace Esp32Meteo
