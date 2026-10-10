package dev.monaka.tracking

import dev.monaka.protocol.v2.*
import java.net.*
import java.nio.ByteBuffer
import java.nio.channels.DatagramChannel
import java.nio.file.Path
import java.time.Instant
import java.util.UUID
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

internal data class ReceiverConfig(val bindIpv4: String, val port: Int, val associationPath: Path,
    val requiredWire: String = "monaka-wire-2.2") {
    fun validate() {
        val p=bindIpv4.split('.');require(p.size==4 && p.all { it.matches(Regex("0|[1-9][0-9]{0,2}")) && it.toInt()<=255 })
        require(bindIpv4!="0.0.0.0" && bindIpv4!="255.255.255.255" && p[0].toInt() !in 224..255)
        require(port in 1..65535 && requiredWire=="monaka-wire-2.2" && associationPath.isAbsolute)
    }
}
internal interface ReceiverSocket : AutoCloseable {
    val endpoint: InetSocketAddress
    /** Exactly one bounded datagram; null denotes cancellation, never a test fallback. */
    fun receive(): ByteArray?
}
internal class Ipv4ReceiverSocket private constructor(private val channel: DatagramChannel): ReceiverSocket {
    override val endpoint get() = channel.localAddress as InetSocketAddress
    private val buffer=ByteBuffer.allocate(4097)
    override fun receive(): ByteArray? { buffer.clear();channel.receive(buffer) ?: return null;return buffer.array().copyOf(buffer.position()) }
    override fun close() { channel.close() }
    companion object {
        fun bind(c: ReceiverConfig): Ipv4ReceiverSocket {
            val channel=DatagramChannel.open(StandardProtocolFamily.INET)
            try {
                channel.setOption(java.net.StandardSocketOptions.SO_REUSEADDR,false)
                channel.bind(InetSocketAddress(InetAddress.getByAddress(c.bindIpv4.split('.').map { it.toInt().toByte() }.toByteArray()),c.port))
                return Ipv4ReceiverSocket(channel)
            } catch(e: Throwable) { channel.close();throw e }
        }
        /** Test-only callers can transfer an already bound isolated channel; no production fallback. */
        internal fun owned(channel: DatagramChannel)=Ipv4ReceiverSocket(channel)
    }
}
internal data class ReceiverHooks(val beforeDecode: ()->Unit = {}, val beforeApply: ()->Unit = {})

/** One socket worker, no receive queue. All state transitions linearize on monitor. */
internal class ReceiverRuntime(private val sockets: (ReceiverConfig)->ReceiverSocket = Ipv4ReceiverSocket::bind,
    private val hooks: ReceiverHooks = ReceiverHooks()): AutoCloseable {
    private val monitor=Object();private val lifecycleLock=ReentrantLock()
    val instance=UUID.randomUUID().toString()
    private var config: ReceiverConfig?=null;private var store: ReceiverStore?=null
    private var association: Association?=null;private var core: TrustedHmdCommonWorldStream?=null
    private var socket: ReceiverSocket?=null;private var worker: Thread?=null
    private var endpoint: InetSocketAddress?=null;private var generation=0L;private var restarts=0L
    private var state="Created";private var shutdown=false;private var persistence="NotLoaded"
    private var lastTransition="Created";private var lastFault: String?=null;private var lastSocketError: String?=null
    private var associationTransition="None";private var lastSeenWire: String?=null;private var lastAcceptedWire: String?=null
    private var lastDecodeError: String?=null;private var worldHigh: Long?=null;private var mappingHigh: Long?=null
    private var retainedWorld: CommonWorldAuthorityPublication?=null;private var retainedMapping: CommonWorldMappingPublication?=null
    private var accepted=0L;private var rejected=0L;private var received=0L;private var decoded=0L;private var validated=0L
    private var duplicates=0L;private var stale=0L;private var gaps=0L;private var legacy=0L;private var staleCompletions=0L
    private var authorizedSession: String?=null;private var lastAccepted: Map<String,Any?>?=null;private var lastRejected: Map<String,Any?>?=null
    private fun inc(v: Long)=if(v==Long.MAX_VALUE) v else v+1
    private fun transition(to: String) { lastTransition="$state->$to";state=to;monitor.notifyAll() }
    private fun fault(code: String) { lastFault=code;transition("Faulted") }
    fun configure(c: ReceiverConfig): String = lifecycleLock.withLock { synchronized(monitor) {
        if(shutdown || state !in listOf("Created","Configured","Stopped")) return@synchronized "LifecycleDenied"
        try {
            c.validate();check(config==null || config!!.associationPath==c.associationPath) { "MetadataPathChangeDenied" }
            if(store==null) {
                val opened=ReceiverStore(c.associationPath);store=opened;association=opened.association;persistence=opened.loadState
                association?.let { a ->
                    val restored=TrustedHmdCommonWorldStream(a.ownerId)
                    opened.checkpoint?.let { checkpoint -> restored.restoreInactive(checkpoint.history);worldHigh=checkpoint.worldHigh;mappingHigh=checkpoint.mappingHigh;retainedWorld=checkpoint.retainedWorld;retainedMapping=checkpoint.retainedMapping }
                    core=restored;associationTransition="Restored"
                }
            }
            config=c;transition("Configured");"Configured"
        } catch(e: Throwable) { persistence="FailClosed";fault(if(e is IllegalArgumentException) "InvalidConfiguration" else "PersistenceLoadFailed");"ConfigurationRejected" }
    } }
    fun associate(a: Association): String = synchronized(monitor) {
        if(shutdown || store==null || state=="Faulted") return@synchronized "AssociationDenied"
        try {
            a.validate();if(association!=null) return@synchronized if(association==a) "AlreadyAssociated" else "AssociationReplacementDenied"
            val next=TrustedHmdCommonWorldStream(a.ownerId)
            store!!.associate(a);association=a;core=next;persistence=store!!.loadState;associationTransition="ExplicitAssociationPersisted";monitor.notifyAll();"Associated"
        } catch(e: Throwable) { associationTransition="AssociationFailed";"AssociationFailed" }
    }
    fun authorizePublisherSession(publisher: String, session: String, clock: String): String = synchronized(monitor) {
        if(shutdown || state=="Faulted" || association==null) return@synchronized "SessionDenied"
        try { uuid(session);uuid(clock) } catch(_: IllegalArgumentException) { return@synchronized "SessionDenied" }
        try {
            if(publisher!=association!!.ownerId || core?.beginAuthorizedPublisherSession(publisher,session,clock)!=true) return@synchronized "SessionDenied"
            authorizedSession=session;persist();monitor.notifyAll();"SessionAuthorized"
        } catch(e: Throwable) { persistence="FailClosed";core?.onTransportDisconnected();fault("CheckpointWriteFailed");"SessionDenied" }
    }
    private fun persist(worldRevision: Long?=worldHigh,mappingRevision: Long?=mappingHigh,
        worldSnapshot: CommonWorldAuthorityPublication?=retainedWorld,mappingSnapshot: CommonWorldMappingPublication?=retainedMapping) {
        val a=association ?: return;val c=core ?: return
        store!!.save(ReceiverCheckpoint(a,c.recoveryHistory(),worldRevision,mappingRevision,worldSnapshot,mappingSnapshot));persistence="AssociationAndHistoryPersisted"
    }
    fun start(): String = lifecycleLock.withLock { synchronized(monitor) {
        if(shutdown || config==null || store==null || state=="Faulted") return@synchronized "StartDenied"
        if(state=="Receiving") return@synchronized "AlreadyReceiving"
        if(worker?.isAlive==true) return@synchronized "WorkerStillStopping"
        try {
            check(generation<Long.MAX_VALUE);generation++;val current=generation
            val opened=sockets(config!!);socket=opened;endpoint=opened.endpoint;transition("Bound")
            val t=Thread({ loop(opened,current) },"common-world-receiver-$current");worker=t;transition("Receiving");t.start();"Receiving"
        } catch(e: Throwable) { socket?.close();socket=null;endpoint=null;lastSocketError=e.javaClass.simpleName;fault("BindFailed");"BindFailed" }
    } }
    private fun loop(s: ReceiverSocket, token: Long) {
        try {
            while(true) {
                val bytes=s.receive() ?: run {
                    synchronized(monitor) { if(token==generation && state=="Receiving") throw java.io.EOFException("SocketEnded") }
                    return
                }
                synchronized(monitor) { if(token==generation && state=="Receiving") { received=inc(received);monitor.notifyAll() } }
                hooks.beforeDecode()
                val value=if(bytes.size in 1..4096) MonakaCodec.decodeEnvelope(bytes) else null
                hooks.beforeApply()
                synchronized(monitor) { process(token,bytes,value) }
                synchronized(monitor) { if(token!=generation || state!="Receiving") return }
            }
        } catch(e: Throwable) { synchronized(monitor) {
            if(token==generation && state=="Receiving") {
                lastSocketError=e.javaClass.simpleName;generation=inc(generation);core?.onTransportDisconnected()
                try { persist() } catch(_: Throwable) { persistence="FailClosed" }
                fault("ReceiveFailed");try { s.close() } catch(_: Throwable) {};socket=null;endpoint=null
            }
        } }
    }
    private fun packet(b: ByteArray, e: Envelope?): Map<String,Any?> {
        val h=e?.let(::headerOf);val a=e?.let(::identity)
        val world=when(e) { is CommonWorldAuthorityPublication->e.world;is CommonWorldRevocation->e.world;is CommonWorldMappingPublication->e.mapping.world;is TrustedHmdCommonPose->e.mapping.world;is TrustedHmdCommonUnavailable->e.mapping.world;is CommonWorldMappingRevocation->e.mapping.world;else->null }
        val mapping=when(e) { is CommonWorldMappingPublication->e.mapping;is TrustedHmdCommonPose->e.mapping;is TrustedHmdCommonUnavailable->e.mapping;is CommonWorldMappingRevocation->e.mapping;else->null }
        return mapOf("bytes" to b.size,"sha256" to hex(hash(b)),"publisher_session" to h?.session_id,"sequence" to h?.sequence,
            "wire_version" to h?.let { "monaka-wire-${it.version.major}.${it.version.minor}" },"identity" to a?.view(),
            "message_type" to e?.javaClass?.simpleName,"world_revision" to world?.coordinate_space?.revision,"mapping_revision" to mapping?.mapping_revision)
    }
    private fun reject(reason: String,stage: String,b: ByteArray,e: Envelope?=null) {
        rejected=inc(rejected);lastRejected=packet(b,e)+mapOf("reason" to reason,"stage" to stage);monitor.notifyAll()
    }
    private fun process(token: Long,b: ByteArray,decodedResult: DecodeResult?) {
        if(token!=generation || state!="Receiving" || shutdown) { staleCompletions=inc(staleCompletions);reject("StaleSocketCompletion","DatagramReceived",b);return }
        if(decodedResult==null) { reject("DatagramLength","DatagramReceived",b);return }
        if(decodedResult is DecodeResult.Failure) { lastDecodeError=decodedResult.code.toString();reject("WireDecode:"+lastDecodeError,"DatagramReceived",b);return }
        val e=(decodedResult as DecodeResult.Success).value;val h=headerOf(e)
        decoded=inc(decoded);lastSeenWire=h?.let { "monaka-wire-${it.version.major}.${it.version.minor}" }
        if(h==null) { reject("WrongProtocol","WireDecoded",b,e);return }
        val a=identity(e)
        if(a==null) { legacy=inc(legacy);reject("LegacyPublisherUnavailable","WireDecoded",b,e);return }
        val expected=association
        if(expected==null) { reject("AssociationRequired","WireDecoded",b,e);return }
        if(a.ownerId!=expected.ownerId) { reject("OwnerMismatch","WireDecoded",b,e);return }
        if(a.worldSpaceId!=expected.worldSpaceId) { reject("WorldMismatch","WireDecoded",b,e);return }
        if(a.sourcePublisherId!=expected.sourcePublisherId) { reject("SourcePublisherMismatch","WireDecoded",b,e);return }
        validated=inc(validated)
        val result=core!!.receive(e,System.nanoTime())
        try {
            if(result in setOf("world","mapping","pose","unavailable","revoked")) {
                var nextWorld=worldHigh;var nextMapping=mappingHigh;var nextWorldSnapshot=retainedWorld;var nextMappingSnapshot=retainedMapping
                if(e is CommonWorldAuthorityPublication) { val r=e.world.coordinate_space.revision;check(worldHigh==null || r>=worldHigh!!);if(worldHigh!=r) { nextMapping=null;nextMappingSnapshot=null };nextWorld=r;nextWorldSnapshot=e }
                if(e is CommonWorldMappingPublication) { val r=e.mapping.mapping_revision;check(mappingHigh==null || r>=mappingHigh!!);nextMapping=r;nextMappingSnapshot=e }
                persist(nextWorld,nextMapping,nextWorldSnapshot,nextMappingSnapshot)
                worldHigh=nextWorld;mappingHigh=nextMapping;retainedWorld=nextWorldSnapshot;retainedMapping=nextMappingSnapshot
                accepted=inc(accepted);lastAcceptedWire=lastSeenWire;lastAccepted=packet(b,e)+mapOf("result" to result,"stage" to "StateApplied");monitor.notifyAll()
            } else {
                if(result in setOf("duplicate","duplicate-observation")) duplicates=inc(duplicates)
                if(result in setOf("old","old-observation")) stale=inc(stale)
                if(result=="gap") gaps=inc(gaps)
                persist();reject(result,"AssociationValidated",b,e)
            }
        } catch(e: Throwable) { persistence="FailClosed";core?.onTransportDisconnected();fault("CheckpointWriteFailed");reject("CheckpointWriteFailed","AssociationValidated",b);try { socket?.close() } catch(_: Throwable) {} }
    }
    fun stop(): String = lifecycleLock.withLock { stopWorker(false) }
    private fun stopWorker(final: Boolean): String {
        val thread=synchronized(monitor) {
            if(shutdown) return "Shutdown"
            if(state=="Stopped" && !final) return "Stopped"
            generation=inc(generation);transition("Stopping")
            try { socket?.close() } catch(e: Throwable) { lastSocketError=e.javaClass.simpleName }
            socket=null;endpoint=null;core?.onTransportDisconnected()
            try { persist() } catch(_: Throwable) { persistence="FailClosed";lastFault="CheckpointWriteFailed" }
            worker
        }
        if(thread!=Thread.currentThread()) thread?.join(5000)
        return synchronized(monitor) {
            if(thread?.isAlive==true) { fault("ShutdownTimeout");return@synchronized "ShutdownTimeout" }
            worker=null
            if(final) { shutdown=true;try { store?.close() } catch(_: Throwable) { lastFault="MetadataCloseFailed" };transition("Shutdown");"Shutdown" }
            else { transition(if(persistence=="FailClosed") "Faulted" else "Stopped");"Stopped" }
        }
    }
    fun restart(c: ReceiverConfig?=null): String = lifecycleLock.withLock {
        if(stopWorker(false)!="Stopped") return@withLock "RestartDenied"
        synchronized(monitor) { restarts=inc(restarts) }
        if(c!=null && configure(c)!="Configured") return@withLock "RestartDenied"
        start()
    }
    override fun close() { lifecycleLock.withLock { stopWorker(true) } }
    fun snapshot(): Map<String,Any?> = synchronized(monitor) {
        mapOf("schema" to "monaka-common-world-receiver-snapshot-1","build" to receiverBuild(),"runtime_instance" to instance,"timestamp" to Instant.now().toString(),
            "lifecycle" to state,"listening" to (state=="Receiving"),"configured_bind_ipv4" to config?.bindIpv4,"configured_port" to config?.port,
            "bound_ipv4" to endpoint?.address?.hostAddress,"bound_port" to endpoint?.port,"socket_generation" to generation,"restart_rebind_count" to restarts,
            "last_socket_error" to lastSocketError,"required_wire_version" to "monaka-wire-2.2","last_seen_wire_version" to lastSeenWire,"last_accepted_wire_version" to lastAcceptedWire,
            "last_decode_error" to lastDecodeError,"association_state" to if(association==null) "Unassociated" else "Associated","retained_association" to association?.view(),
            "association_metadata_schema" to "MCWRXA01","association_metadata_version" to 1,"history_schema" to "MCWRXO01","history_version" to 1,
            "persistence_state" to persistence,"last_association_transition" to associationTransition,"accepted_world_revision_high_water" to worldHigh,
            "accepted_mapping_revision_high_water" to mappingHigh,"mapping_revision_scope_world" to worldHigh,"sequence_high_water" to core?.sequenceHighWater(),
            "retained_world" to retainedWorld?.world,"retained_mapping" to retainedMapping?.mapping,"world_current" to (core?.currentWorld()!=null),"mapping_current" to (core?.currentMapping()!=null),
            "pose_current" to (core?.candidate()!=null),"authorized_publisher_session" to authorizedSession,"last_accepted_packet" to lastAccepted,"last_rejected_packet" to lastRejected,
            "counters" to mapOf("DatagramReceived" to received,"WireDecoded" to decoded,"AssociationValidated" to validated,"StateApplied" to accepted,"accepted" to accepted,"rejected" to rejected,"duplicate" to duplicates,"stale_out_of_order" to stale,"gap" to gaps,"legacy" to legacy,"stale_socket_completion" to staleCompletions),
            "last_lifecycle_transition" to lastTransition,"last_fault" to lastFault,"shutdown" to shutdown,
            "acceptance_semantics" to "receiver-local revision validation and Common World state apply; no application ACK or durable application commit",
            "sender_delivery_semantics" to "ordered best-effort local submission")
    }
    internal fun await(predicate: (Map<String,Any?>)->Boolean): Map<String,Any?> = synchronized(monitor) {
        val end=System.nanoTime()+5_000_000_000L
        while(!predicate(snapshot())) { val left=end-System.nanoTime();check(left>0) { "ObservationTimeout" };monitor.wait(left/1_000_000L,(left%1_000_000L).toInt()) }
        snapshot()
    }
}
