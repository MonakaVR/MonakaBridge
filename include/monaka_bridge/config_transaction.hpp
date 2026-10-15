#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
namespace mb {
// Internal config I/O, separate from tracking/wire/revision-store contracts.
std::string readConfigBytes(const std::filesystem::path&);
void commitConfigDocument(const std::filesystem::path& target,
 const std::optional<std::string>& expected,const std::string& candidate,
 const std::function<void(const char*)>& testHook={});
}
