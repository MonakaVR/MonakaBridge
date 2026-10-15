#pragma once
#include <array>
#include <string_view>
namespace monaka::input {
enum class Capability { Position, Orientation, LinearVelocity, AngularVelocity, LinearAcceleration, BatteryFraction, Charging };
inline constexpr std::array<std::string_view,7> CapabilityTokens{"position","orientation","linear_velocity","angular_velocity","linear_acceleration","battery_fraction","charging"};
}
