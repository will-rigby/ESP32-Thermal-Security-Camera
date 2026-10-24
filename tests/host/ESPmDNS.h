#pragma once
struct HostMDNS { bool begin(const char*) { return true; } void end() {} };
inline HostMDNS MDNS;
