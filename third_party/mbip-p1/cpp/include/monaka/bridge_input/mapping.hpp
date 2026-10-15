#pragma once
#include "monaka/input/capabilities.hpp"
namespace monaka::bridge_input {
enum class TranslationClass { PASS_THROUGH, RENAMED, DERIVED, UNSUPPORTED, REJECTED };
struct Mapping { monaka::input::Capability source; std::string_view target; TranslationClass classification; };
inline constexpr std::array<Mapping,7> CapabilityMapping{{
    {monaka::input::Capability::Position,"position",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::Orientation,"orientation",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::LinearVelocity,"linear_velocity",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::AngularVelocity,"angular_velocity",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::LinearAcceleration,"linear_acceleration",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::BatteryFraction,"battery_fraction",TranslationClass::PASS_THROUGH},
    {monaka::input::Capability::Charging,"charging",TranslationClass::PASS_THROUGH}
}};
}
