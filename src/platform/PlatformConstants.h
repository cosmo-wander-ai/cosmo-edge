#pragma once

#include <string>

namespace cosmo::platform {

// Watchdog paths
inline const std::string kWatchdogDevice = "/dev/watchdog";

// DNS & Route paths
inline const std::string kResolvConfTmp  = "/etc/resolv.conf.tmp";
inline const std::string kResolvConf     = "/etc/resolv.conf";
inline const std::string kDhclientLeases = "/var/lib/dhcp/dhclient.leases";
inline const std::string kProcNetRoute   = "/proc/net/route";

// Netplan config paths
inline const std::string kNetplanDir          = "/etc/netplan";
inline const std::string kNetplanFailSafeFile = "/etc/netplan/01-failsafe.yaml";
inline const std::string kNetplanFailSafeBak  = "/etc/netplan/01-failsafe.yaml.bak";
inline const std::string kNetplanConfigFile   = "/etc/netplan/99-netcfg.yaml";
inline const std::string kPackagedFailSafeBak = "/appfs/cosmo_wander/cwai_data/scripts/01-failsafe.yaml.bak";

}  // namespace cosmo::platform
