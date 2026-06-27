#include "mqtt_client.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
#include "firmware_logic.h"
#include "ha_discovery.h"
#include "local_button.h"
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
bool mqttResetControlsConfigured = false;
const char* resetCredentialsSource = nullptr;

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
  const char* source = resetCredentialsSource ? resetCredentialsSource : "unknown";
  resetCredentialsRequested = false;
  resetCredentialsSource = nullptr;
  if (otaInProgress) {
    Serial.printf("Credentials reset request from %s skipped: OTA update is in progress\n", source);
    return;
  }

  resetCredentialsInProgress = true;
  Serial.printf("Credentials reset request accepted from %s\n", source);

  const bool statusOk = mqttClientInstance.publish(kStatusTopic, "resetting_credentials", true);
  Serial.printf("MQTT publish %s%s%s = %sresetting_credentials%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                kStatusTopic,
                serialReset(),
                serialStyle(SerialStyle::Value),
                serialReset(),
                serialOkFailed(statusOk));
  flushMqttClientOnly(kTelemetryFlushMs);

  const bool configCleared = clearRuntimeConfig();
  if (!configCleared) {
    Serial.println("Credentials reset aborted: runtime config could not be cleared");
    const bool failureStatusOk =
        mqttClientInstance.publish(kStatusTopic, "online; degraded: credentials_reset_failed", true);
    Serial.printf("MQTT publish %s%s%s = %sonline; degraded: credentials_reset_failed%s retained: %s\n",
                  serialStyle(SerialStyle::Topic),
                  kStatusTopic,
                  serialReset(),
                  serialStyle(SerialStyle::Warning),
                  serialReset(),
                  serialOkFailed(failureStatusOk));
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
      requestCredentialsReset("mqtt");
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
    Serial.printf("Stay-awake command received on %s%s%s: %s\n",
                  serialStyle(SerialStyle::Topic),
                  kStayAwakeTopic,
                  serialReset(),
                  serialTrueFalse(stayAwakeRequested));
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
    Serial.printf("MQTT subscribe %s%s%s qos=0 for status confirmation: %s\n",
                  serialStyle(SerialStyle::Topic),
                  kStatusTopic,
                  serialReset(),
                  serialOkFailed(subscribed));
    statusConfirmationSubscribed = subscribed;
    confirmationAvailable = subscribed;
  }

  const bool statusOk = mqttClientInstance.publish(kStatusTopic, payload, true);
  Serial.printf("MQTT publish %s%s%s = %s%s%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                kStatusTopic,
                serialReset(),
                serialStyle(SerialStyle::Value),
                payload,
                serialReset(),
                serialOkFailed(statusOk));
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
  Serial.printf("MQTT broker echo for %s%s%s = %s%s%s: %s%s%s\n",
                serialStyle(SerialStyle::Topic),
                kStatusTopic,
                serialReset(),
                serialStyle(SerialStyle::Value),
                payload,
                serialReset(),
                serialStyle(confirmed ? SerialStyle::Success : SerialStyle::Warning),
                confirmed ? "confirmed" : "not confirmed",
                serialReset());
  return confirmed;
}

bool mqttPostOnlineBudgetExceeded(uint32_t started, const char* phase) {
  const uint32_t elapsedMs = millis() - started;
  if (elapsedMs < kMqttPostOnlineSetupBudgetMs) {
    return false;
  }

  Serial.printf("MQTT post-online setup budget exceeded before %s after %lu/%lu ms\n",
                phase,
                static_cast<unsigned long>(elapsedMs),
                static_cast<unsigned long>(kMqttPostOnlineSetupBudgetMs));
  logBootPhase("mqtt_post_online_budget_exceeded");
  return true;
}

void recordSetupPhase(const char* phase) {
  if (telemetryPublishCompleted) {
    publishBootPhase(phase);
    return;
  }

  logBootPhase(phase);
}

}  // namespace

PubSubClient& mqttClient() {
  return mqttClientInstance;
}

void logBootPhase(const char* phase) {
  const char* safePhase = (phase && phase[0]) ? phase : "unknown";
  Serial.printf("Boot phase: %s millis=%lu reset=%s wake=%s boots=%lu sleep_entries=%lu heap=%lu\n",
                safePhase,
                static_cast<unsigned long>(millis()),
                resetReasonName(esp_reset_reason()),
                wakeupCauseName(esp_sleep_get_wakeup_cause()),
                static_cast<unsigned long>(rtcBootCount),
                static_cast<unsigned long>(rtcSleepEntryCount),
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

bool publishBootPhase(const char* phase) {
  const char* safePhase = (phase && phase[0]) ? phase : "unknown";
  if (!mqttClientInstance.connected()) {
    Serial.printf("MQTT boot phase %s skipped: client disconnected\n", safePhase);
    return false;
  }

  char payload[256];
  if (!formatInto(payload,
                  sizeof(payload),
                  "MQTT boot phase payload",
                  "phase=%s millis=%lu reset=%s wake=%s boots=%lu sleep_entries=%lu heap=%lu",
                  safePhase,
                  static_cast<unsigned long>(millis()),
                  resetReasonName(esp_reset_reason()),
                  wakeupCauseName(esp_sleep_get_wakeup_cause()),
                  static_cast<unsigned long>(rtcBootCount),
                  static_cast<unsigned long>(rtcSleepEntryCount),
                  static_cast<unsigned long>(ESP.getFreeHeap()))) {
    Serial.printf("MQTT boot phase %s skipped: payload too long\n", safePhase);
    return false;
  }

  const String fullTopic = topic("/diagnostic/boot_phase");
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), payload, true);
  Serial.printf("MQTT publish %s%s%s = %s%s%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                fullTopic.c_str(),
                serialReset(),
                serialStyle(SerialStyle::Muted),
                payload,
                serialReset(),
                serialOkFailed(ok));
  return ok;
}

bool requestCredentialsReset(const char* source) {
  const char* resetSource = (source && source[0]) ? source : "unknown";
  if (resetCredentialsInProgress) {
    Serial.printf("Credentials reset request from %s ignored: reset already in progress\n", resetSource);
    return false;
  }

  if (otaInProgress) {
    Serial.printf("Ignoring credentials reset request from %s while OTA update is in progress\n", resetSource);
    return false;
  }

  resetCredentialsRequested = true;
  resetCredentialsSource = resetSource;
  Serial.printf("Credentials reset command queued from %s\n", resetSource);
  return true;
}

void serviceCredentialResetRequests() {
  handlePendingResetCredentials();
}

void serviceMqttAndOta() {
  handleOta();
  serviceLocalButton();
  mqttClientInstance.loop();
  serviceCredentialResetRequests();
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
  mqttClientInstance.setSocketTimeout(kMqttSocketTimeoutSeconds);
  wifiClient.setConnectionTimeout(kMqttNetworkTimeoutMs);
  const bool bufferConfigured = mqttClientInstance.setBufferSize(kMqttBufferSize);
  mqttResetControlsConfigured = false;
  statusConfirmationSubscribed = false;

  Serial.printf("MQTT server: %s%s:%u%s\n",
                serialStyle(SerialStyle::Value),
                config.mqttHost,
                config.mqttPort,
                serialReset());
  Serial.printf("MQTT socket timeout: %u seconds, network timeout: %lu ms\n",
                static_cast<unsigned int>(kMqttSocketTimeoutSeconds),
                static_cast<unsigned long>(kMqttNetworkTimeoutMs));
  Serial.printf("MQTT buffer size %u bytes: %s\n",
                static_cast<unsigned int>(kMqttBufferSize),
                serialOkFailed(bufferConfigured));
  Serial.printf("MQTT client ID prefix: %s%s%s\n", serialStyle(SerialStyle::Topic), kTopicPrefix, serialReset());
  Serial.printf("MQTT status topic: %s%s%s\n", serialStyle(SerialStyle::Topic), kStatusTopic, serialReset());
  Serial.printf("MQTT stay-awake topic: %s%s%s\n", serialStyle(SerialStyle::Topic), kStayAwakeTopic, serialReset());
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
      Serial.printf("%sMQTT connected%s in %lu ms\n",
                    serialStyle(SerialStyle::Success),
                    serialReset(),
                    static_cast<unsigned long>(millis() - started));
      logBootPhase("mqtt_connected");
      const bool statusPublished = mqttClientInstance.publish(kStatusTopic, "online", true);
      Serial.printf("MQTT publish %s%s%s = %sonline%s retained: %s\n",
                    serialStyle(SerialStyle::Topic),
                    kStatusTopic,
                    serialReset(),
                    serialStyle(SerialStyle::Value),
                    serialReset(),
                    serialOkFailed(statusPublished));
      if (!statusPublished) {
        logBootPhase("status_online_failed");
        return false;
      }
      logBootPhase("status_online");

      const uint32_t postOnlineStarted = millis();
      logBootPhase("stay_awake_subscribe_start");
      const bool subscribed = mqttClientInstance.subscribe(kStayAwakeTopic, 1);
      Serial.printf("MQTT subscribe %s%s%s qos=1: %s\n",
                    serialStyle(SerialStyle::Topic),
                    kStayAwakeTopic,
                    serialReset(),
                    serialOkFailed(subscribed));
      if (!subscribed) {
        logBootPhase("stay_awake_subscribe_failed");
        return false;
      }
      logBootPhase("stay_awake_subscribe_done");
      if (mqttPostOnlineBudgetExceeded(postOnlineStarted, "retained stay-awake wait")) {
        return false;
      }
      waitForRetainedStayAwakeCommand();
      if (mqttPostOnlineBudgetExceeded(postOnlineStarted, "Home Assistant status subscribe")) {
        return false;
      }
      logBootPhase("ha_status_subscribe_start");
      const bool haSubscribed = mqttClientInstance.subscribe(kHaStatusTopic, 0);
      Serial.printf("MQTT subscribe %s%s%s qos=0: %s\n",
                    serialStyle(SerialStyle::Topic),
                    kHaStatusTopic,
                    serialReset(),
                    serialOkFailed(haSubscribed));
      if (!haSubscribed) {
        logBootPhase("ha_status_subscribe_failed");
        return false;
      }
      logBootPhase("ha_status_subscribe_done");
      if (mqttPostOnlineBudgetExceeded(postOnlineStarted, "telemetry handoff")) {
        return false;
      }
      configureMqttResetControls();
      publishHomeAssistantDiscovery();
      logBootPhase("mqtt_ready_for_telemetry");
      return true;
    }

    Serial.printf("%sMQTT connect failed%s, rc=%d\n",
                  serialStyle(SerialStyle::Error),
                  serialReset(),
                  mqttClientInstance.state());
    waitWithMqttAndOta(kMqttRetryDelayMs, "MQTT connect retry wait");
  }

  Serial.println("MQTT connection failed");
  return false;
}

bool configureMqttResetControls() {
  if (mqttResetControlsConfigured) {
    Serial.println("MQTT reset controls already configured");
    return true;
  }

  recordSetupPhase("reset_controls_start");
  const bool resetCommandCleared = mqttClientInstance.publish(kResetCredentialsTopic, "", true);
  Serial.printf("MQTT clear retained %s%s%s command: %s\n",
                serialStyle(SerialStyle::Topic),
                kResetCredentialsTopic,
                serialReset(),
                serialOkFailed(resetCommandCleared));
  if (!resetCommandCleared) {
    recordSetupPhase("reset_controls_failed");
    return false;
  }

  const bool resetSubscribed = mqttClientInstance.subscribe(kResetCredentialsTopic, 1);
  Serial.printf("MQTT subscribe %s%s%s qos=1: %s\n",
                serialStyle(SerialStyle::Topic),
                kResetCredentialsTopic,
                serialReset(),
                serialOkFailed(resetSubscribed));
  mqttResetControlsConfigured = resetSubscribed;
  recordSetupPhase(mqttResetControlsConfigured ? "reset_controls_done" : "reset_controls_failed");
  return mqttResetControlsConfigured;
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
  Serial.printf("MQTT publish %s%s%s = %s%s%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                fullTopic.c_str(),
                serialReset(),
                serialStyle(SerialStyle::Value),
                payload,
                serialReset(),
                serialOkFailed(ok));
  return ok;
}

bool publishText(const char* suffix, const char* value, bool retained) {
  const String fullTopic = topic(suffix);
  const bool ok = mqttClientInstance.publish(fullTopic.c_str(), value, retained);
  Serial.printf("MQTT publish %s%s%s = %s%s%s retained=%s: %s\n",
                serialStyle(SerialStyle::Topic),
                fullTopic.c_str(),
                serialReset(),
                serialStyle(SerialStyle::Value),
                value,
                serialReset(),
                serialTrueFalse(retained),
                serialOkFailed(ok));
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
  Serial.printf("HA discovery payload %s%s%s: payload=%s%u%s bytes, packet_estimate=%s%u/%u%s bytes\n",
                serialStyle(SerialStyle::Topic),
                discoveryTopic,
                serialReset(),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned int>(payloadLength),
                serialReset(),
                serialStyle(SerialStyle::Value),
                static_cast<unsigned int>(packetBytes),
                static_cast<unsigned int>(kMqttBufferSize),
                serialReset());
  if (!mqttPacketFits(discoveryTopic, payloadLength, kMqttBufferSize, kMqttMaxHeaderBytes)) {
    Serial.printf("HA discovery skipped %s: MQTT packet estimate exceeds buffer\n", discoveryTopic);
    return false;
  }

  const bool ok = mqttClientInstance.publish(discoveryTopic, payload, true);
  Serial.printf("HA discovery publish %s%s%s retained: %s\n",
                serialStyle(SerialStyle::Topic),
                discoveryTopic,
                serialReset(),
                serialOkFailed(ok));
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
  logBootPhase("stay_awake_wait_start");
  const uint32_t started = millis();
  while (!stayAwakeCommandReceived && millis() - started < kRetainedCommandWaitMs) {
    serviceMqttAndOta();
    delay(10);
  }

  if (!stayAwakeCommandReceived) {
    Serial.println("No retained stay-awake command received before timeout; defaulting to one publish then sleep");
    logBootPhase("stay_awake_wait_timeout");
  } else {
      Serial.printf("Retained stay-awake command applied: %s\n", serialTrueFalse(stayAwakeRequested));
    Serial.printf("Stay-awake retained value means this boot will %s\n",
                  stayAwakeRequested ? "remain awake" : "return to deep sleep after publishing");
    logBootPhase(stayAwakeRequested ? "stay_awake_true" : "stay_awake_false");
  }
}

}  // namespace Esp32Meteo
