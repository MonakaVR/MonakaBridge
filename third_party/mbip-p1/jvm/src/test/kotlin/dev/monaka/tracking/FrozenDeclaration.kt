package dev.monaka.tracking

/** Declared support, never inferred from current modality or nullable samples. */
class DeclaredTrackerCapabilities(components: Collection<String>) {
	val components: Set<String> = java.util.Collections.unmodifiableSet(LinkedHashSet(components))
	init {
		require(components.size == this.components.size) { "Duplicate capability" }
		require(this.components.all { it in SUPPORTED }) { "Unknown capability" }
	}
	val supportsQ: Boolean get() = "orientation" in components
	val supportsSixDof: Boolean get() = "position" in components && supportsQ
	override fun equals(other: Any?): Boolean = other is DeclaredTrackerCapabilities && components == other.components
	override fun hashCode(): Int = components.hashCode()
	companion object {
		private val SUPPORTED = setOf("position", "orientation", "linear_velocity", "angular_velocity",
			"linear_acceleration", "battery_fraction", "charging")
		fun orientationOnly() = DeclaredTrackerCapabilities(listOf("orientation"))
	}
}
