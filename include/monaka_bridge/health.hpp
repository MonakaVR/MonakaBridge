#pragma once
#include "bridge.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace mb {
// Diagnostic snapshot only. Call on the Bridge owner thread; the writer never
// reads registry/config/cache objects or feeds data back into tracking.
nlohmann::json healthSnapshot(const Bridge&, std::int64_t now, std::int64_t unixMs);

struct HealthFileOperations {
 std::function<bool(const std::filesystem::path&, std::string_view)> write;
 std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> replace;
};
struct HealthRetry { unsigned attempts=20; std::chrono::milliseconds delay{10}; };
class HealthWriter {
public:
 explicit HealthWriter(std::filesystem::path, HealthFileOperations = {}, HealthRetry = {});
 ~HealthWriter();
 HealthWriter(const HealthWriter&)=delete;
 HealthWriter& operator=(const HealthWriter&)=delete;
 // Best effort, one pending snapshot, try_lock only; never file I/O or retry.
 bool publish(nlohmann::json) noexcept;
 void stop(); // Discard pending, interrupt retry wait, join in-flight file operation.
 std::uint64_t written() const { return written_.load(); }
 std::uint64_t writeFailures() const { return writeFailures_.load(); }
 std::uint64_t replaceFailures() const { return replaceFailures_.load(); }
 std::uint64_t dropped() const { return dropped_.load(); }
private:
 void run();
 bool stopping();
 std::filesystem::path target_, temporary_;
 HealthFileOperations operations_;
 HealthRetry retry_;
 std::mutex mutex_;
 std::condition_variable ready_;
 std::optional<nlohmann::json> pending_;
 bool stop_=false;
 std::atomic_uint64_t written_{0},writeFailures_{0},replaceFailures_{0},dropped_{0};
 std::thread worker_;
};
}
