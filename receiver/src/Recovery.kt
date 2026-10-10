package dev.monaka.tracking

import dev.monaka.protocol.v2.*

/** Inactive restart history only: never restores live handles or authorizes a session. */
internal data class WorldRecovery(val revisions: Map<String, Long>, val retiredWorlds: Set<String>,
    val retiredCalibrations: Set<String>, val exhausted: Boolean)
internal data class SourceRecovery(val space: SourceSpaceAuthorityEpoch, val retired: Boolean,
    val generationRetired: Boolean, val observation: TrustedHmdCommonPose?)
internal data class StreamRecovery(val world: WorldRecovery, val sessions: Set<String>,
    val sources: List<SourceRecovery>, val exhausted: Boolean)

internal data class Association(val ownerId: String, val worldSpaceId: String, val sourcePublisherId: String) {
    fun validate() { listOf(ownerId, worldSpaceId, sourcePublisherId).forEach(::validId) }
    fun view()=mapOf("owner_id" to ownerId,"world_space_id" to worldSpaceId,"source_publisher_id" to sourcePublisherId)
}
internal fun validId(s: String) {
    require(s.isNotBlank() && s.toByteArray(Charsets.UTF_8).size in 1..96)
    require(s.codePoints().noneMatch { Character.isISOControl(it) || it in 0xD800..0xDFFF })
}
internal fun uuid(s: String) { require(java.util.UUID.fromString(s).toString() == s) }
internal fun identity(m: Envelope): Association? = when (m) {
    is CommonWorldAuthorityPublication -> m.source_publisher_id?.let { Association(m.world.owner_id, m.world.coordinate_space.id, it) }
    is CommonWorldMappingPublication -> m.source_publisher_id?.let { Association(m.mapping.world.owner_id, m.mapping.world.coordinate_space.id, it) }
    is TrustedHmdCommonPose -> m.source_publisher_id?.let { Association(m.mapping.world.owner_id, m.mapping.world.coordinate_space.id, it) }
    is TrustedHmdCommonUnavailable -> m.source_publisher_id?.let { Association(m.mapping.world.owner_id, m.mapping.world.coordinate_space.id, it) }
    is CommonWorldMappingRevocation -> m.source_publisher_id?.let { Association(m.mapping.world.owner_id, m.mapping.world.coordinate_space.id, it) }
    is CommonWorldRevocation -> m.source_publisher_id?.let { Association(m.world.owner_id, m.world.coordinate_space.id, it) }
    else -> null
}
internal fun headerOf(m: Envelope): AuthorityHeader? = when (m) {
    is CommonWorldAuthorityPublication -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    is CommonWorldMappingPublication -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    is TrustedHmdCommonPose -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    is TrustedHmdCommonUnavailable -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    is CommonWorldMappingRevocation -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    is CommonWorldRevocation -> AuthorityHeader(m.version,m.publisher_id,m.session_id,m.clock_id,m.sequence,m.timestamp_ns,m.sent_at_ns,m.timestamp_kind)
    else -> null
}
internal fun wireBytes(e: Envelope): ByteArray = (MonakaCodec.encodeEnvelope(e) as EncodeResult.Success).value
internal fun wireValue(b: ByteArray): Envelope = (MonakaCodec.decodeEnvelope(b) as DecodeResult.Success).value
