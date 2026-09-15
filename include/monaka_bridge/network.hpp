#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
namespace mb {
std::int64_t monotonicNs();std::string uuid();
struct Datagram{std::string bytes,peer;};
class Udp {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit Udp(std::uint16_t port=0);~Udp();Udp(const Udp&)=delete;Udp& operator=(const Udp&)=delete;
 bool send(std::uint16_t port,std::string_view)noexcept;
 std::optional<Datagram> receive();
 std::uint16_t port()const;
};
}
