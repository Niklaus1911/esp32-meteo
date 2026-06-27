#pragma once

#include <Arduino.h>

namespace Esp32Meteo {

extern bool stayAwakeRequested;
extern bool stayAwakeCommandReceived;
extern bool homeAssistantDiscoveryRequested;
extern bool otaInProgress;
extern bool telemetryPublishCompleted;
extern bool statusConfirmationPending;
extern bool statusConfirmationReceived;
extern bool statusConfirmationSubscribed;
extern const char* statusConfirmationExpected;
extern uint32_t telemetryPublishCompletedMs;
extern uint32_t lastPublishMs;
extern uint32_t rtcBootCount;
extern uint32_t rtcSleepEntryCount;

}  // namespace Esp32Meteo
