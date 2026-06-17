#include "mqtt_client.h"

#include <WiFi.h>

#include "config.h"
#include "firmware_logic.h"
#include "ha_discovery.h"
#include "ota_service.h"
#include "runtime_config.h"
#include "runtime_state.h"
#include "util.h"

namespace Esp32Meteo {

namespace {

WiFiClient wifiClient;
PubSubClient mqttClientInstance(wifiClient);
bool resetCredentialsRequested = false;
bool resetCredentialsInProgress = false;

void flushMqttClientOnly(uint32_t durationMs) {
  Serial.printf("Flushing MQTT for credentials reset for %lu ms\n", static_cast<unsigned long>(durationMs));
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    mqttClientInstance.loop();
    delay(10);
  }
  Serial.println("MQTT credentials-reset flush complete");
}

void resetCredentialsAndRestart() {
  resetCredentialsRequested = false;
  if (otaInProgress) {
    Serial.println("Credentials reset skipped: OTA update is in progress");
    return;
  }

  resetCredentialsInProgress = true;
  Serial.printf("Credentials reset command accepted on %s\n", kResetCredentialsTopic);

  const bool statusOk = mqttClientInstance.publish(kStatusTopic, "resetting_credentials", true);
  Serial.printf("MQTT publish %s = resetting_credentials retained: %s\n",
                kStatusTopic,
                statusOk ? "ok" : "FAILED");
  flushMqttClientOnly(kTelemetryFlushMs);

  const bool configCleared = clearRuntimeConfig();
  if (!configCleared) {
    Serial.println("Credentials reset aborted: runtime config could not be cleared");
    const bool failureStatusOk =
        mqttClientInstance.publish(kStatusTopic, "online; degraded: credentials_reset_failed", true);
    Serial.printf("MQTT publish %s = online; degraded: credentials_reset_failed retained: %s\n",
                  kStatusTopic,
                  failureStatusOk ? "ok" : "FAILED");
    flushMqttClientOnly(kTelemetryFlushMs);
    resetCredentialsInProgress = false;
    return;
  }

  if (mqttClientInstance.connected()) {
    mqttClientInstance.disconnect();
    Serial.println("MQTT disconnected for credentials reset");
  }

  bool wifiCleared = WiFi.disconnect(false, true);
  if (!wifiCleared) {
    wifiCleared = WiFi.eraseAP();
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  Serial.printf("Runtime config cleared: %s\n", configCleared ? "yes" : "no");
  Serial.printf("Saved WiFi credentials cleared from NVS: %s\n", wifiCleared ? "yes" : "no");
  Serial.println("Restarting after credentials reset; provisioning portal will start on next boot");
  Serial.flush();
  ESP.restart();
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

  if (strcmp(receivedTopic, kResetCredentialsTopic) == 0) {
    if (parseResetCredentialsPayload(reinterpret_cast<const char*>(payload), length)) {
      if (otaInProgress) {
        Serial.println("Ignoring credentials reset command while OTA update is in progress");
      } else {
        resetCredentialsRequested = true;
        Serial.printf("Credentials reset command queued from %s\n", kResetCredentialsTopic);
      }
    } else {
      Serial.printf("Ignoring invalid reset-credentials command payload on %s, length=%u\n",
                    kResetCredentialsTopic,
                    length);
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

void handlePendingResetCredentials() {
  if (resetCredentialsRequested && !resetCredentialsInProgress) {
    resetCredentialsAndRestart();
  }
}

bool publishRetainedStatusAndWaitForEcho(const char* payload, uint32_t timeoutMs) {
  statusConfirmationExpected = payload;
  statusConfirmationReceived = false;
  statusConfirmationPending = true;

  bool confirmationAvailable = true;
  if (!statusConfirmationSubscribed) {
    const bool subscribed = mqttClientInstance.subscribe(kStatusTopic, 0);
    Serial.printf("MQTT subscribe %s qos=0 for status confirmation: %s\n",
                  kStatusTopic,
                  subscribed ? "ok" : "FAILED");
    statusConfirmationSubscribed = subscribed;
    confirmationAvailable = subscribed;
  }

  const bool statusOk = mqttClientInstance.publish(kStatusTopic, payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n",
                kStatusTopic,
                payload,
                statusOk ? "ok" : "FAILED");
  if (!statusOk || !confirmationAvailable) {
    statusConfirmationPending = false;
    statusConfirmationExpected = nullptr;
    return false;
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
  handlePendingResetCredentials();
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
  const RuntimeConfig& config = runtimeConfig();
  mqttClientInstance.setServer(config.mqttHost, config.mqttPort);
  mqttClientInstance.setCallback(mqttCallback);
  const bool bufferConfigured = mqttClientInstance.setBufferSize(kMqttBufferSize);

  Serial.printf("MQTT server: %s:%u\n", config.mqttHost, config.mqttPort);
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
    const bool authConfigured = config.mqttUsername[0] || config.mqttPassword[0];
    const bool connected = authConfigured
                               ? mqttClientInstance.connect(clientId.c_str(), config.mqttUsername, config.mqttPassword)
                               : mqttClientInstance.connect(clientId.c_str());
    if (connected) {
      Serial.printf("MQTT connected in %lu ms\n", static_cast<unsigned long>(millis() - started));
      const bool statusPublished = mqttClientInstance.publish(kStatusTopic, "online", true);
      Serial.printf("MQTT publish %s = online retained: %s\n", kStatusTopic, statusPublished ? "ok" : "FAILED");
      if (!statusPublished) {
        return false;
      }
      const bool subscribed = mqttClientInstance.subscribe(kStayAwakeTopic, 1);
      Serial.printf("MQTT subscribe %s qos=1: %s\n", kStayAwakeTopic, subscribed ? "ok" : "FAILED");
      if (!subscribed) {
        return false;
      }
      const bool resetCommandCleared = mqttClientInstance.publish(kResetCredentialsTopic, "", true);
      Serial.printf("MQTT clear retained %s command: %s\n",
                    kResetCredentialsTopic,
                    resetCommandCleared ? "ok" : "FAILED");
      if (!resetCommandCleared) {
        return false;
      }
      const bool resetSubscribed = mqttClientInstance.subscribe(kResetCredentialsTopic, 1);
      Serial.printf("MQTT subscribe %s qos=1: %s\n",
                    kResetCredentialsTopic,
                    resetSubscribed ? "ok" : "FAILED");
      if (!resetSubscribed) {
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

bool publishFloat(const char* suffix, float value) {
  const String fullTopic = topic(suffix);
  if (isnan(value) || isinf(value)) {
    Serial.printf("MQTT skip %s: invalid reading\n", fullTopic.c_str());
    return true;
  }

  char payload[24];
  dtostrf(value, 0, 8, payload);
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), payload, true);
  Serial.printf("MQTT publish %s = %s retained: %s\n", fullTopic.c_str(), payload, ok ? "ok" : "FAILED");
  return ok;
}

bool publishText(const char* suffix, const char* value, bool retained) {
  const String fullTopic = topic(suffix);
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), value, retained);
  Serial.printf("MQTT publish %s = %s retained=%s: %s\n",
                fullTopic.c_str(),
                value,
                retained ? "true" : "false",
                ok ? "ok" : "FAILED");
  return ok;
}

bool publishDiscoveryPayload(const char* component, const char* objectId, const char* payload) {
  char discoveryTopic[160];
  if (!formatInto(discoveryTopic,
                  sizeof(discoveryTopic),
                  "Home Assistant discovery topic",
                  "%s/%s/%s/config",
                  kHaDiscoveryPrefix,
                  component,
                  objectId)) {
    Serial.printf("HA discovery skipped for %s/%s: topic too long\n", component, objectId);
    return false;
  }

  const size_t payloadLength = strlen(payload);
  const size_t packetBytes = kMqttMaxHeaderBytes + 2 + strlen(discoveryTopic) + payloadLength;
  Serial.printf("HA discovery payload %s: payload=%u bytes, packet_estimate=%u/%u bytes\n",
                discoveryTopic,
                static_cast<unsigned int>(payloadLength),
                static_cast<unsigned int>(packetBytes),
                static_cast<unsigned int>(kMqttBufferSize));
  if (!mqttPacketFits(discoveryTopic, payloadLength, kMqttBufferSize, kMqttMaxHeaderBytes)) {
    Serial.printf("HA discovery skipped %s: MQTT packet estimate exceeds buffer\n", discoveryTopic);
    return false;
  }

  const bool ok = mqttClientInstance.publish(discoveryTopic, payload, true);
  Serial.printf("HA discovery publish %s retained: %s\n", discoveryTopic, ok ? "ok" : "FAILED");
  return ok;
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
