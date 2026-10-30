#pragma once
#include "calibration.hpp"
#include "registry.hpp"
#include <filesystem>
#include "monaka/bridge_input/normalizer.hpp"
namespace mb {
struct CapabilityProfile {
 std::string id,provenance;
 std::vector<std::string> inputDeclaration,outputDeclaration;
 monaka::bridge_input::Mode inputMode=monaka::bridge_input::Mode::STRICT;
 monaka::bridge_input::Mode outputMode=monaka::bridge_input::Mode::STRICT;
};
enum class Policy { Steamvr,Monaka,Both,Disabled };
Policy parsePolicy(const std::string&);std::string policyName(Policy);
struct Config {
 // Loaded raw snapshot is local config I/O metadata; never emitted on wire.
 std::string sourceDocument;
 std::filesystem::path sourcePath;
 std::string bridgeId="monaka-bridge";
 std::uint32_t revision=1;
 std::int64_t timeoutNs=500000000;
 Policy policy=Policy::Steamvr;
 std::map<std::string,Profile> profiles;
 std::map<Key,Binding> bindings;
 std::map<Key,CapabilityProfile> contracts;
};
Config parseConfigDocument(const std::string&);
Config loadConfig(const std::filesystem::path&);
void saveConfig(const std::filesystem::path&,const Config&);
void validateConfig(const Config&);
// Compares local documents with optional Identity/diagnostic defaults, while
// retaining every unknown member. It never bypasses exact-snapshot CAS.
bool equivalentConfigDocuments(const std::string&,const std::string&);
// Supported output channels from an explicit backend declaration and existing
// transform contract. Never inspect pose values or current validity here.
std::vector<std::string> projectOutputCapabilities(const std::vector<std::string>&,
                                                  const CapabilityProfile&,const Binding&);
// Explicit migration requires a destination mapping/space; retains originals and backup.
void migrateLegacy(const std::filesystem::path& route,const std::filesystem::path& alignment,
                   const std::filesystem::path& destination,Config);
std::string runtimeSerial(const std::string& source,const std::string& tracker);
}
