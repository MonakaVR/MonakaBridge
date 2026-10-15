package dev.monaka.bridgeinput
import dev.monaka.input.Capability
enum class TranslationClass { PASS_THROUGH, RENAMED, DERIVED, UNSUPPORTED, REJECTED }
data class Mapping(val source: Capability, val target: String, val classification: TranslationClass)
internal val capabilityMapping = listOf(
    Mapping(Capability.POSITION,"position",TranslationClass.PASS_THROUGH),
    Mapping(Capability.ORIENTATION,"orientation",TranslationClass.PASS_THROUGH),
    Mapping(Capability.LINEAR_VELOCITY,"linear_velocity",TranslationClass.PASS_THROUGH),
    Mapping(Capability.ANGULAR_VELOCITY,"angular_velocity",TranslationClass.PASS_THROUGH),
    Mapping(Capability.LINEAR_ACCELERATION,"linear_acceleration",TranslationClass.PASS_THROUGH),
    Mapping(Capability.BATTERY_FRACTION,"battery_fraction",TranslationClass.PASS_THROUGH),
    Mapping(Capability.CHARGING,"charging",TranslationClass.PASS_THROUGH)
)
