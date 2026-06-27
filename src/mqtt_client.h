#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

namespace Esp32Meteo {

PubSubClient& mqttClient();
void logBootPhase(const char* phase);
bool publishBootPhase(const char* phase);
bool requestCredentialsReset(const char* source);
void serviceCredentialResetRequests();
void serviceMqttAndOta();
void waitWithMqttAndOta(uint32_t durationMs, const char* description);
bool connectMqtt();
bool configureMqttResetControls();
bool publishFloat(const char* suffix, float value);
bool publishText(const char* suffix, const char* value, bool retained = true);
bool publishDiscoveryPayload(const char* component, const char* objectId, const char* payload);
void flushMqtt(uint32_t durationMs);
bool publishSleepStatusWithConfirmation();
void waitForRetainedStayAwakeCommand();

}  // namespace Esp32Meteo
