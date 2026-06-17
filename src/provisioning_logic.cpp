#include "provisioning_logic.h"

#include "runtime_config.h"

namespace Esp32Meteo {

bool provisioningIpModeIsStatic(const char* ipMode) {
  return ipMode && ipMode[0] == 's' && ipMode[1] == 't' && ipMode[2] == 'a' &&
         ipMode[3] == 't' && ipMode[4] == 'i' && ipMode[5] == 'c' && ipMode[6] == '\0';
}

const char* provisioningIpModeForConfig(bool hasStaticIp) {
  return hasStaticIp ? kProvisioningIpModeStatic : kProvisioningIpModeDhcp;
}

const char* provisioningSubnetForStaticMode(const char* submittedSubnet) {
  return submittedSubnet && submittedSubnet[0] ? submittedSubnet : kRuntimeConfigDefaultSubnet;
}

const char* provisioningStaticFieldForDhcpMode() {
  return "";
}

}  // namespace Esp32Meteo
