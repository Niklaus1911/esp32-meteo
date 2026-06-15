#include "runtime_state.h"

namespace Esp32Meteo {

bool stayAwakeRequested = false;
bool stayAwakeCommandReceived = false;
bool homeAssistantDiscoveryRequested = false;
bool otaInProgress = false;
bool telemetryPublishCompleted = false;
bool statusConfirmationPending = false;
bool statusConfirmationReceived = false;
bool statusConfirmationSubscribed = false;
const char* statusConfirmationExpected = nullptr;
uint32_t telemetryPublishCompletedMs = 0;
uint32_t lastPublishMs = 0;

}  // namespace Esp32Meteo
