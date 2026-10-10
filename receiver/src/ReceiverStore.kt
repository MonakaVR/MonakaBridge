package dev.monaka.tracking

import dev.monaka.protocol.v2.*
import java.io.*
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.*
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

internal fun hash(b: ByteArray) = MessageDigest.getInstance("SHA-256").digest(b)
internal fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it.toInt() and 255) }
internal data class ReceiverCheckpoint(val association: Association, val history: StreamRecovery,
    val worldHigh: Long?, val mappingHigh: Long?, val retainedWorld: CommonWorldAuthorityPublication?,
    val retainedMapping: CommonWorldMappingPublication?)

/** A distinct receiver metadata file and receiver history file; never sender MCWREV01/02. */
internal class ReceiverStore(val path: Path) : AutoCloseable {
    private val order = path.resolveSibling(path.fileName.toString()+".order")
    private val marker = path.resolveSibling(path.fileName.toString()+".order-required")
    private val lockPath = path.resolveSibling(path.fileName.toString()+".lock")
    private val channel: FileChannel
    private val lock: java.nio.channels.FileLock
    var association: Association? = null; private set
    var checkpoint: ReceiverCheckpoint? = null; private set
    var loadState = "NotLoaded"; private set
    init {
        require(path.isAbsolute && Files.isDirectory(path.parent) && !path.fileName.toString().endsWith(".rev"))
        listOf(path,order,marker,lockPath).forEach { require(!Files.isSymbolicLink(it)) }
        val previous = Files.exists(lockPath)
        channel = FileChannel.open(lockPath,StandardOpenOption.CREATE,StandardOpenOption.WRITE)
        try {
            lock = channel.tryLock() ?: error("MetadataOwned")
            if (!Files.exists(path)) {
                check(!previous && !Files.exists(order) && !Files.exists(marker)) { "MetadataMissing" }
                atomic(path, record("MCWRXA01") { writeBoolean(false) })
            }
            if (Files.exists(marker)) check(Files.size(marker)==1L && Files.readAllBytes(marker).contentEquals(byteArrayOf(1))) { "CheckpointMarker" }
            association = parse(path,"MCWRXA01",1024) { if (flag()) tuple() else null }
            if (Files.exists(order)) {
                checkpoint = parse(order,"MCWRXO01",1048576) { checkpoint() }
                check(association != null && checkpoint!!.association == association) { "CheckpointAssociationMismatch" }
            } else check(!Files.exists(marker)) { "CheckpointMissing" }
            loadState = if (association == null) "UnassociatedLoaded" else "AssociationLoaded"
        } catch (e: Throwable) { channel.close(); throw e }
    }
    fun associate(a: Association) {
        a.validate(); check(association == null || association == a) { "AssociationReplacementDenied" }
        if (association == a) return
        atomic(path,record("MCWRXA01") { writeBoolean(true); tuple(a) })
        association = a; loadState = "AssociationPersisted"
    }
    fun save(c: ReceiverCheckpoint) {
        check(c.association == association)
        val bytes = record("MCWRXO01") { checkpoint(c) }
        check(bytes.size <= 1048576) { "CheckpointCapacity" }
        // A missing future checkpoint cannot be mistaken for first startup.
        if (!Files.exists(marker)) atomic(marker,byteArrayOf(1))
        atomic(order,bytes); checkpoint = c
    }
    private fun atomic(p: Path, bytes: ByteArray) {
        val temp=p.resolveSibling(p.fileName.toString()+".tmp")
        check(!Files.isSymbolicLink(p) && !Files.isSymbolicLink(temp))
        FileChannel.open(temp,StandardOpenOption.CREATE,StandardOpenOption.TRUNCATE_EXISTING,StandardOpenOption.WRITE).use { f ->
            val b=ByteBuffer.wrap(bytes); while(b.hasRemaining()) check(f.write(b)>0); f.force(true)
        }
        // Unsupported atomic replacement is a failure, never a weaker fallback.
        Files.move(temp,p,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING)
    }
    override fun close() { if (lock.isValid) lock.release(); channel.close() }
    private fun record(magic: String, f: DataOutputStream.()->Unit): ByteArray {
        val out=ByteArrayOutputStream(); DataOutputStream(out).use { d -> d.write(magic.toByteArray(Charsets.US_ASCII));d.writeInt(1);d.f() }
        val payload=out.toByteArray();return payload+hash(payload)
    }
    private fun <T> parse(p: Path, magic: String, max: Int, f: DataInputStream.()->T): T {
        val size=Files.size(p);check(size in 44..max.toLong()) { "MetadataSize" }
        val bytes=Files.readAllBytes(p);check(bytes.size.toLong()==size) { "MetadataChanged" }
        val payload=bytes.copyOfRange(0,bytes.size-32)
        check(MessageDigest.isEqual(hash(payload),bytes.copyOfRange(bytes.size-32,bytes.size))) { "MetadataIntegrity" }
        return DataInputStream(ByteArrayInputStream(payload)).use { d ->
            val m=ByteArray(8);d.readFully(m);check(m.contentEquals(magic.toByteArray(Charsets.US_ASCII))) { "MetadataMagic" }
            check(d.readInt()==1) { "MetadataVersion" };val v=d.f();check(d.available()==0) { "MetadataTrailingBytes" };v
        }
    }
    private fun DataOutputStream.str(s: String) { val b=s.toByteArray(Charsets.UTF_8);check(b.size<=4096);writeInt(b.size);write(b) }
    private fun DataInputStream.str(): String {
        val n=readInt();check(n in 0..4096 && n<=available());val b=ByteArray(n);readFully(b)
        return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(b)).toString()
    }
    private fun DataOutputStream.tuple(a: Association) { a.validate();str(a.ownerId);str(a.worldSpaceId);str(a.sourcePublisherId) }
    private fun DataInputStream.tuple()=Association(str(),str(),str()).also { it.validate() }
    private fun DataOutputStream.optional(v: Long?) { writeBoolean(v!=null);if(v!=null)writeLong(v) }
    private fun DataInputStream.flag(): Boolean = readUnsignedByte().also { check(it in 0..1) { "MetadataFlag" } }==1
    private fun DataInputStream.optional(): Long? = if(flag()) readLong().also { check(it in 0..4294967295L) } else null
    private fun DataOutputStream.envelope(e: Envelope?) { writeBoolean(e!=null);if(e!=null) { val b=wireBytes(e);check(b.size<=4096);writeInt(b.size);write(b) } }
    private fun DataInputStream.envelope(): Envelope? = if(!flag()) null else {
        val n=readInt();check(n in 1..4096 && n<=available());val b=ByteArray(n);readFully(b);wireValue(b)
    }
    private fun DataInputStream.count(): Int=readInt().also { check(it in 0..128) }
    private fun DataOutputStream.strings(v: Set<String>) { check(v.size<=128);writeInt(v.size);v.sorted().forEach { str(it) } }
    private fun DataInputStream.strings(): Set<String> { val n=count();val v=List(n) { str() };check(v.toSet().size==n);return v.toSet() }
    private fun DataOutputStream.checkpoint(c: ReceiverCheckpoint) {
        tuple(c.association);optional(c.worldHigh);optional(c.mappingHigh);envelope(c.retainedWorld);envelope(c.retainedMapping)
        val h=c.history;writeBoolean(h.exhausted);writeBoolean(h.world.exhausted)
        check(h.world.revisions.size<=128);writeInt(h.world.revisions.size)
        h.world.revisions.toSortedMap().forEach { (id,rev) -> str(id);writeLong(rev) }
        strings(h.world.retiredWorlds);strings(h.world.retiredCalibrations);strings(h.sessions)
        check(h.sources.size<=128);writeInt(h.sources.size)
        h.sources.sortedWith(compareBy({it.space.sourceId},{it.space.sourceSessionEpoch})).forEach {
            str(it.space.sourceId);str(it.space.sourceSessionEpoch);str(it.space.sourceSpaceId);writeLong(it.space.generation)
            writeBoolean(it.retired);writeBoolean(it.generationRetired);envelope(it.observation)
        }
    }
    private fun DataInputStream.checkpoint(): ReceiverCheckpoint {
        val a=tuple();val high=optional();val mapHigh=optional()
        val w=envelope();val m=envelope();check(w==null || w is CommonWorldAuthorityPublication);check(m==null || m is CommonWorldMappingPublication)
        val exhausted=flag();val worldExhausted=flag();val rev=linkedMapOf<String,Long>()
        repeat(count()) { val id=str();validId(id);val v=readLong();check(v in 0..4294967295L && rev.put(id,v)==null) }
        val retiredW=strings();val retiredC=strings();val sessions=strings()
        retiredW.forEach(::validId);retiredC.forEach(::validId);sessions.forEach(::uuid)
        val sources=List(count()) {
            val s=SourceSpaceAuthorityEpoch(str(),str(),str(),readLong());validId(s.sourceId);validId(s.sourceSessionEpoch);validId(s.sourceSpaceId)
            val retired=flag();val generationRetired=flag();val pose=envelope();check(pose==null || pose is TrustedHmdCommonPose)
            SourceRecovery(s,retired,generationRetired,pose as TrustedHmdCommonPose?)
        }
        check(sources.map { it.space.sourceId to it.space.sourceSessionEpoch }.toSet().size==sources.size)
        (listOfNotNull(w,m)+sources.mapNotNull { it.observation }).forEach { check(identity(it)==a && headerOf(it)?.publisher_id==a.ownerId) }
        check(high==rev[a.worldSpaceId]);check((w as CommonWorldAuthorityPublication?)?.world?.coordinate_space?.revision==high)
        check((m as CommonWorldMappingPublication?)?.mapping?.mapping_revision==mapHigh)
        check(m==null || m.mapping.world==w?.world)
        val h=StreamRecovery(WorldRecovery(rev,retiredW,retiredC,worldExhausted),sessions,sources,exhausted)
        return ReceiverCheckpoint(a,h,high,mapHigh,w,m as CommonWorldMappingPublication?)
    }
}
