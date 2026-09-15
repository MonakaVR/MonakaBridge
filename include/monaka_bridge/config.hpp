#pragma once
#include "calibration.hpp"
#include "registry.hpp"
#include <filesystem>
namespace mb {
enum class Policy { Steamvr,Monaka,Both,Disabled };
Policy parsePolicy(const std::string&);std::string policyName(Policy);
struct Config {
 std::string bridgeId="monaka-bridge";
 std::uint32_t revision=1;
 std::int64_t timeoutNs=500000000;
 Policy policy=Policy::Steamvr;
 std::map<std::string,Profile> profiles;
 std::map<Key,Binding> bindings;
};
Config loadConfig(const std::filesystem::path&);
void saveConfig(const std::filesystem::path&,const Config&);
void validateConfig(const Config&);
// Explicit migration requires a destination mapping/space; retains originals and backup.
void migrateLegacy(const std::filesystem::path& route,const std::filesystem::path& alignment,
                   const std::filesystem::path& destination,Config);
std::string runtimeSerial(const std::string& source,const std::string& tracker);
}
