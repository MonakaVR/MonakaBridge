package dev.monaka.bridgeinput

import dev.monaka.input.*
import dev.monaka.protocol.v2.Envelope
import java.util.Collections

enum class Mode { STRICT, LEGACY_DYNAMIC }
data class Binding(val identity: Identity, val session: String, val declaration: CapabilitySet, val provenance: String)
data class CoreRecord(val message: Envelope, val source: Projection, val channels: Map<String, Channel>)

class Normalizer(private val binding: Binding, private val mode: Mode = Mode.STRICT) {
    init { if (binding.provenance.isEmpty()) throw ContractError("MissingDeclaration") }
    fun normalize(message: Envelope): CoreRecord {
        val input = CapabilityContract.project(message)
        val output = if (mode == Mode.LEGACY_DYNAMIC) {
            if (!input.capabilities.subsetOf(binding.declaration)) throw ContractError("UndeclaredCapability")
            CapabilityContract.withCapabilities(message, binding.declaration)
        } else message
        val facts = CapabilityContract.project(output)
        CapabilityContract.checkBinding(facts, binding.identity, binding.session, binding.declaration)
        val channels = capabilityMapping.associate { row ->
            if (row.classification != TranslationClass.PASS_THROUGH) throw ContractError("UnsupportedMapping")
            row.target to facts.channels.getValue(row.source)
        }
        return CoreRecord(output, facts, Collections.unmodifiableMap(channels))
    }
}
