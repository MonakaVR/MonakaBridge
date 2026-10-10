#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <cstdint>

namespace monaka_bridge {
enum class CommonWorldExecutionMode { Production, Test };
struct CommonWorldActivationConfig {
 bool enabled = false, valid = true;
 CommonWorldExecutionMode mode = CommonWorldExecutionMode::Production;
 std::string source, error, transport, codec, endpoint, ownerId, worldSpaceId, sourcePublisher;
 std::filesystem::path revisionPath;
 std::uint32_t ipv4 = 0;
 std::uint16_t port = 0;
 std::size_t queueCapacity = 64, retryLimit = 3;
};
// No environment/property/global settings and no fallback. Missing source means OFF.
CommonWorldActivationConfig parseCommonWorldActivationConfig(std::optional<std::string> json,
 const std::filesystem::path& source = {}, bool validateDisabled = false);
CommonWorldActivationConfig loadCommonWorldActivationConfig(const std::optional<std::filesystem::path>&);
}
