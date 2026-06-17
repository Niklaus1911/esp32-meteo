#pragma once

namespace Esp32Meteo {

constexpr const char* kProvisioningIpModeDhcp = "dhcp";
constexpr const char* kProvisioningIpModeStatic = "static";

bool provisioningIpModeIsStatic(const char* ipMode);
const char* provisioningIpModeForConfig(bool hasStaticIp);
const char* provisioningSubnetForStaticMode(const char* submittedSubnet);
const char* provisioningStaticFieldForDhcpMode();

}  // namespace Esp32Meteo
