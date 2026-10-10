package dev.monaka.tracking

import dev.monaka.protocol.v2.*
import com.google.gson.Gson
import java.net.*
import java.nio.ByteBuffer
import java.nio.channels.DatagramChannel
import java.nio.file.*
import java.util.concurrent.*
import kotlin.concurrent.thread

private var cases=0;private var assertions=0
private fun ck(v: Boolean) { assertions++;check(v) }
private fun eq(a: Any?,b: Any?) { assertions++;check(a==b) { "$a != $b" } }
private fun run(name: String,f: ()->Unit) { f();cases++;println("PASS $name") }
private fun failure(f: ()->Unit) { var failed=false;try { f() } catch(_: Throwable) { failed=true };ck(failed) }
private val root=Path.of(System.getProperty("receiver.root"))
private val output=root.resolve("receiver/build/test-fixtures")
private val base=Files.readAllLines(root.resolve("third_party/monaka-protocol-v22/fixtures/v2.1/ordered-streams/common-normal.jsonl")).map { wireValue(it.toByteArray()) }
private const val PUB="source-publisher-A"
private const val S2="22222222-2222-4222-8222-222222222222"
private const val S3="33333333-3333-4333-8333-333333333333"
private fun upgrade(e: Envelope,pub: String=PUB): Envelope = when(e) {
    is CommonWorldAuthorityPublication -> e.copy(version=Version(2,2),source_publisher_id=pub)
    is CommonWorldMappingPublication -> e.copy(version=Version(2,2),source_publisher_id=pub)
    is TrustedHmdCommonPose -> e.copy(version=Version(2,2),source_publisher_id=pub)
    is TrustedHmdCommonUnavailable -> e.copy(version=Version(2,2),source_publisher_id=pub)
    is CommonWorldMappingRevocation -> e.copy(version=Version(2,2),source_publisher_id=pub)
    is CommonWorldRevocation -> e.copy(version=Version(2,2),source_publisher_id=pub)
    else -> error("WrongFixture")
}
private val world=upgrade(base[0]) as CommonWorldAuthorityPublication
private val mapping=upgrade(base[1]) as CommonWorldMappingPublication
private val pose=upgrade(base[2]) as TrustedHmdCommonPose
private val assoc=identity(world)!!
private fun count(s: Map<String,Any?>,name: String)=(s["counters"] as Map<*,*>)[name] as Long
private fun reason(s: Map<String,Any?>)=(s["last_rejected_packet"] as Map<*,*>)["reason"]
private fun state(s: Map<String,Any?>)=listOf(s["retained_association"],s["retained_world"],s["retained_mapping"],s["accepted_world_revision_high_water"],s["accepted_mapping_revision_high_water"],s["sequence_high_water"],s["world_current"],s["mapping_current"],s["pose_current"])
private class Fake: ReceiverSocket {
    override val endpoint=InetSocketAddress("127.0.0.1",32123)
    val queue=ArrayBlockingQueue<Any>(8);@Volatile private var closed=false
    override fun receive(): ByteArray? { val p=queue.take();if(closed)return null;if(p is Throwable)throw p;return p as ByteArray }
    override fun close() { closed=true;queue.clear();queue.offer(byteArrayOf()) }
}
private class Rig(val path: Path=Files.createTempDirectory(output,"case-").resolve("association.bin"),
    val fake: Fake=Fake(),val hooks: ReceiverHooks=ReceiverHooks()): AutoCloseable {
    val r=ReceiverRuntime({fake},hooks);val config=ReceiverConfig("127.0.0.1",32123,path)
    init { eq(r.configure(config),"Configured") }
    fun setup() { eq(r.associate(assoc),"Associated");eq(r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");eq(r.start(),"Receiving") }
    fun send(bytes: ByteArray): Map<String,Any?> { val before=r.snapshot();val n=count(before,"accepted")+count(before,"rejected")+1;fake.queue.put(bytes);return r.await { count(it,"accepted")+count(it,"rejected")>=n } }
    fun send(e: Envelope)=send(wireBytes(e))
    fun live() { setup();eq((send(world)["last_accepted_packet"] as Map<*,*>)["result"],"world");eq((send(mapping)["last_accepted_packet"] as Map<*,*>)["result"],"mapping") }
    override fun close() { r.close() }
}
private fun fresh(w: CommonWorldAuthorityPublication,session: String=S2)=w.copy(session_id=session,sequence=0,world=w.world.copy(world_epoch="fresh-$session",coordinate_space=w.world.coordinate_space.copy(revision=w.world.coordinate_space.revision+1)))
private fun freshMap(w: CommonWorldAuthorityPublication)=mapping.copy(session_id=w.session_id,sequence=1,mapping=mapping.mapping.copy(world=w.world,calibration_epoch="cal-${w.world.world_epoch}"))
private fun accepted(s: Map<String,Any?>,value: String) { eq((s["last_accepted_packet"] as Map<*,*>)["result"],value) }

fun main() {
    Files.createDirectories(output)
    run("valid explicit config") { ReceiverConfig("192.0.2.10",30111,output.toAbsolutePath().resolve("unused.bin")).validate();ck(true) }
    for(ip in listOf("hostname","::1","127.0.00.1","256.1.1.1","0.0.0.0","224.0.0.1")) run("invalid IPv4 $ip") { failure { ReceiverConfig(ip,30111,output.toAbsolutePath().resolve("unused.bin")).validate() } }
    for(port in listOf(0,-1,65536)) run("invalid port $port") { failure { ReceiverConfig("127.0.0.1",port,output.toAbsolutePath().resolve("unused.bin")).validate() } }
    run("legacy production configuration denied") { failure { ReceiverConfig("127.0.0.1",30111,output.toAbsolutePath().resolve("unused.bin"),"monaka-wire-2.1").validate() } }
    run("created runtime binds nothing") { ReceiverRuntime().use { eq(it.snapshot()["lifecycle"],"Created");eq(it.snapshot()["bound_port"],null);ck(receiverBuild()["commit"]!!.length==40) } }
    run("unassociated packet cannot authorize itself") { Rig().use { eq(it.r.start(),"Receiving");eq(reason(it.send(world)),"AssociationRequired");eq(it.r.snapshot()["retained_world"],null) } }
    run("explicit association and session separately required") { Rig().use { eq(it.r.associate(assoc),"Associated");eq(it.r.start(),"Receiving");eq(reason(it.send(world)),"unauthorized");eq(it.r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");accepted(it.send(world),"world") } }
    run("explicit association is idempotent and immutable") { Rig().use { eq(it.r.associate(assoc),"Associated");val bytes=Files.readAllBytes(it.path);eq(it.r.associate(assoc),"AlreadyAssociated");eq(it.r.associate(assoc.copy(sourcePublisherId="source-publisher-B")),"AssociationReplacementDenied");ck(bytes.contentEquals(Files.readAllBytes(it.path))) } }
    run("invalid explicit association leaves unassociated") { Rig().use { eq(it.r.associate(assoc.copy(sourcePublisherId="")),"AssociationFailed");eq(it.r.snapshot()["association_state"],"Unassociated") } }
    run("valid 2.2 bootstrap world mapping pose") { Rig().use { it.live();accepted(it.send(pose),"pose");eq(it.r.snapshot()["pose_current"],true) } }
    for(dimension in listOf("owner","world","publisher")) run("$dimension mismatch never mutates Core or highwater") {
        Rig().use { it.live();val before=state(it.r.snapshot());val wrong=when(dimension) {
            "owner"->world.copy(world=world.world.copy(owner_id="foreign-owner"))
            "world"->world.copy(world=world.world.copy(coordinate_space=world.world.coordinate_space.copy(id="foreign-world")))
            else->world.copy(source_publisher_id="source-publisher-B") }
            val s=it.send(wrong);eq(reason(s),when(dimension){"owner"->"OwnerMismatch";"world"->"WorldMismatch";else->"SourcePublisherMismatch"});eq(state(s),before);eq(count(s,"accepted"),2L)
        }
    }
    run("actual wire A B differ only canonical publisher") { val a=wireBytes(world);val b=wireBytes(world.copy(source_publisher_id="source-publisher-B"));ck(!a.contentEquals(b));eq((wireValue(b) as CommonWorldAuthorityPublication).copy(source_publisher_id=PUB),world) }
    run("wire publisher Unicode and case remain exact opaque identity") { val w=world.copy(source_publisher_id="Publisher-É/東京");eq(identity(wireValue(wireBytes(w))),identity(w));Rig().use { x ->eq(x.r.associate(identity(w)!!),"Associated");eq(x.r.authorizePublisherSession(w.publisher_id,w.session_id,w.clock_id),"SessionAuthorized");eq(x.r.start(),"Receiving");accepted(x.send(w),"world");val before=state(x.r.snapshot());eq(reason(x.send(w.copy(source_publisher_id="publisher-É/東京"))),"SourcePublisherMismatch");eq(state(x.r.snapshot()),before) } }
    run("malformed truncated wire rejected before Core") { Rig().use { it.live();val before=state(it.r.snapshot());val s=it.send(wireBytes(world).copyOf(13));ck(reason(s).toString().startsWith("WireDecode:"));eq(state(s),before) } }
    run("unsupported version rejected") { Rig().use { it.live();val before=state(it.r.snapshot());val raw=String(wireBytes(world)).replace("\"minor\":2","\"minor\":3");eq(reason(it.send(raw.toByteArray())),"WireDecode:UnsupportedVersion");eq(state(it.r.snapshot()),before) } }
    run("malformed UTF8 rejected") { Rig().use { it.setup();ck(reason(it.send(byteArrayOf(0xff.toByte()))).toString().startsWith("WireDecode:")) } }
    run("invalid required publisher rejected") { Rig().use { it.setup();val raw=String(wireBytes(world)).replace("source-publisher-A","");eq(reason(it.send(raw.toByteArray())),"WireDecode:OutOfRange") } }
    run("duplicate JSON framing rejected") { Rig().use { it.setup();val raw=String(wireBytes(world)).dropLast(1)+",\"source_publisher_id\":\"source-publisher-A\"}";eq(reason(it.send(raw.toByteArray())),"WireDecode:DuplicateKey") } }
    run("oversize datagram rejected") { Rig().use { it.setup();eq(reason(it.send(ByteArray(4097))),"DatagramLength") } }
    run("empty datagram rejected") { Rig().use { it.setup();eq(reason(it.send(byteArrayOf())),"DatagramLength") } }
    run("binary64 precision survives wire without Float projection") { val values=listOf(1.0000000000000002,-2.0000000000000004,0.10000000000000002);val p=pose.copy(source_position=values,common_position=values);val d=wireValue(wireBytes(p)) as TrustedHmdCommonPose;eq(d.source_position.map { it.toRawBits() },values.map { it.toRawBits() });eq(d.common_position.map { it.toRawBits() },values.map { it.toRawBits() });Rig().use { it.live();accepted(it.send(p),"pose") } }
    run("signed zero follows unchanged legacy JVM codec behavior") { val old=(base[2] as TrustedHmdCommonPose).copy(source_position=listOf(-0.0,1.0,2.0),common_position=listOf(-0.0,1.0,2.0));val legacy=wireValue(wireBytes(old)) as TrustedHmdCommonPose;val current=wireValue(wireBytes(upgrade(old))) as TrustedHmdCommonPose;eq(legacy.source_position.map { it.toRawBits() },current.source_position.map { it.toRawBits() });eq(current.source_position.first().toRawBits(),0.0.toRawBits());eq(current.source_position,current.common_position) }
    run("legacy pure decode remains publisher unavailable") { val old=base[0] as CommonWorldAuthorityPublication;eq(old.source_publisher_id,null);eq((wireValue(wireBytes(old)) as CommonWorldAuthorityPublication).source_publisher_id,null) }
    run("legacy production rejected no mutation or fabrication") { Rig().use { it.live();val before=state(it.r.snapshot());eq(reason(it.send(base[0])),"LegacyPublisherUnavailable");eq(state(it.r.snapshot()),before) } }
    run("legacy same named unknown field never promotes identity") { val raw=String(wireBytes(base[0])).dropLast(1)+",\"source_publisher_id\":\"source-publisher-A\"}";eq((wireValue(raw.toByteArray()) as CommonWorldAuthorityPublication).source_publisher_id,null);Rig().use { it.setup();eq(reason(it.send(raw.toByteArray())),"LegacyPublisherUnavailable") } }
    run("duplicate UDP world ignored without refresh") { Rig().use { it.setup();it.send(world);val before=state(it.r.snapshot());eq(reason(it.send(world)),"duplicate");eq(state(it.r.snapshot()),before);eq(count(it.r.snapshot(),"accepted"),1L) } }
    run("duplicate resend sent_at metadata ignored") { Rig().use { it.setup();it.send(world);eq(reason(it.send(world.copy(sent_at_ns=world.sent_at_ns+1))),"duplicate") } }
    run("lower sequence out of order cannot rewind") { Rig().use { it.live();it.send(pose);val before=state(it.r.snapshot());eq(reason(it.send(world)),"old");eq(state(it.r.snapshot()),before);eq(count(it.r.snapshot(),"stale_out_of_order"),1L) } }
    run("same sequence immutable conflict closes trust preserves highwater") { Rig().use { it.setup();it.send(world);eq(reason(it.send(world.copy(world=world.world.copy(world_epoch="conflict")))),"conflict");eq(it.r.snapshot()["world_current"],false);eq(it.r.snapshot()["accepted_world_revision_high_water"],world.world.coordinate_space.revision) } }
    run("gap closes trust missing packet cannot repair") { Rig().use { it.live();eq(reason(it.send(pose.copy(sequence=10))),"gap");eq(reason(it.send(pose)),"closed");eq(it.r.snapshot()["world_current"],false) } }
    run("increasing mapping revision requires ordered publication") { Rig().use { it.live();val next=mapping.copy(sequence=2,mapping=mapping.mapping.copy(mapping_revision=mapping.mapping.mapping_revision+1));accepted(it.send(next),"mapping");eq(it.r.snapshot()["accepted_mapping_revision_high_water"],next.mapping.mapping_revision) } }
    run("lower mapping revision with next sequence never rolls state back") { Rig().use { it.live();val before=it.r.snapshot()["accepted_mapping_revision_high_water"];eq(reason(it.send(mapping.copy(sequence=2,mapping=mapping.mapping.copy(mapping_revision=mapping.mapping.mapping_revision-1)))),"untrusted");eq(it.r.snapshot()["accepted_mapping_revision_high_water"],before) } }
    run("world replacement requires old world revocation") { Rig().use { it.live();eq(reason(it.send(fresh(world).copy(session_id=world.session_id,sequence=2))),"untrusted");eq(it.r.snapshot()["accepted_world_revision_high_water"],world.world.coordinate_space.revision) } }
    run("ordered revoke then increasing world accepted") { Rig().use { it.live();accepted(it.send(pose),"pose");val rv=Files.readAllLines(root.resolve("third_party/monaka-protocol-v22/fixtures/v2.1/ordered-streams/common-world-replace.jsonl")).map { wireValue(it.toByteArray()) }.filterIsInstance<CommonWorldRevocation>().first();accepted(it.send(upgrade(rv)),"revoked");val w=fresh(world).copy(session_id=world.session_id,sequence=4);accepted(it.send(w),"world");eq(it.r.snapshot()["accepted_world_revision_high_water"],w.world.coordinate_space.revision) } }
    run("stop cancels blocked receive and retains association") { Rig().use { it.live();eq(it.r.stop(),"Stopped");eq(it.r.snapshot()["retained_association"],assoc.view());eq(it.r.snapshot()["bound_port"],null) } }
    run("repeated stop and shutdown idempotent") { Rig().use { it.setup();eq(it.r.stop(),"Stopped");eq(it.r.stop(),"Stopped");it.r.close();it.r.close();eq(it.r.start(),"StartDenied") } }
    run("restart requires fresh stream but never replaces tuple") { val dir=Files.createTempDirectory(output,"restart-");val fakes=ArrayDeque<Fake>().also { it.add(Fake());it.add(Fake()) };val first=fakes.first();ReceiverRuntime({fakes.removeFirst()}).use { r -> eq(r.configure(ReceiverConfig("127.0.0.1",32123,dir.resolve("association.bin"))),"Configured");eq(r.associate(assoc),"Associated");eq(r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");eq(r.start(),"Receiving");first.queue.put(wireBytes(world));r.await {count(it,"accepted")==1L};eq(r.restart(),"Receiving");eq(r.snapshot()["retained_association"],assoc.view());eq(r.snapshot()["restart_rebind_count"],1L);ck((r.snapshot()["socket_generation"] as Long)>=3) } }
    run("process restart restores tuple highwater tombstones session history") { val path=Files.createTempDirectory(output,"process-").resolve("association.bin");Rig(path).use { it.live() };Rig(path).use { x ->eq(x.r.snapshot()["retained_association"],assoc.view());eq(x.r.snapshot()["accepted_world_revision_high_water"],world.world.coordinate_space.revision);eq(x.r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionDenied");eq(x.r.authorizePublisherSession(world.publisher_id,S2,world.clock_id),"SessionAuthorized");eq(x.r.start(),"Receiving");val w=fresh(world);accepted(x.send(w),"world");accepted(x.send(freshMap(w)),"mapping") } }
    run("publisher mismatch after process restart remains rejected") { val path=Files.createTempDirectory(output,"foreign-").resolve("association.bin");Rig(path).use { it.live() };Rig(path).use { x ->eq(x.r.start(),"Receiving");val before=state(x.r.snapshot());eq(reason(x.send(world.copy(source_publisher_id="source-publisher-B"))),"SourcePublisherMismatch");eq(state(x.r.snapshot()),before) } }
    run("old world revision cannot revive after process restart") { val path=Files.createTempDirectory(output,"old-").resolve("association.bin");Rig(path).use { it.live() };Rig(path).use { x ->eq(x.r.authorizePublisherSession(world.publisher_id,S2,world.clock_id),"SessionAuthorized");eq(x.r.start(),"Receiving");eq(reason(x.send(world.copy(session_id=S2))),"untrusted");eq(x.r.snapshot()["accepted_world_revision_high_water"],world.world.coordinate_space.revision) } }
    for(kind in listOf("corrupt","version","truncated","missing","history-corrupt","history-missing")) run("$kind persistence fails closed never defaults or clears") {
        val path=Files.createTempDirectory(output,"bad-").resolve("association.bin");Rig(path).use { it.live() }
        val target=if(kind.startsWith("history"))path.resolveSibling("association.bin.order") else path
        val raw=Files.readAllBytes(target);when(kind) {
            "missing","history-missing"->Files.delete(target)
            "truncated"->Files.write(target,raw.copyOf(10))
            "version"->{raw[11]=2;val payload=raw.copyOf(raw.size-32);Files.write(target,payload+hash(payload))}
            else->{raw[20]=(raw[20].toInt() xor 1).toByte();Files.write(target,raw)} }
        val before=if(Files.exists(target))Files.readAllBytes(target) else null
        ReceiverRuntime().use { r ->eq(r.configure(ReceiverConfig("127.0.0.1",32123,path)),"ConfigurationRejected");eq(r.start(),"StartDenied");eq(r.snapshot()["persistence_state"],"FailClosed") }
        if(before==null)ck(!Files.exists(target)) else ck(before.contentEquals(Files.readAllBytes(target)))
    }
    run("metadata exclusive ownership enforced") { Rig().use { x ->ReceiverRuntime().use { r ->eq(r.configure(x.config),"ConfigurationRejected");eq(r.start(),"StartDenied") } } }
    run("checkpoint write failure faults closed without publishing accepted highwater") { Rig().use { x ->x.setup();Files.createDirectory(x.path.resolveSibling("association.bin.order.tmp"));val s=x.send(world);eq(reason(s),"CheckpointWriteFailed");eq(s["lifecycle"],"Faulted");eq(s["retained_world"],null);eq(s["accepted_world_revision_high_water"],null);eq(count(s,"accepted"),0L);eq(s["world_current"],false) } }
    run("invalid session authorization cannot fault receiver") { Rig().use { x ->eq(x.r.associate(assoc),"Associated");eq(x.r.authorizePublisherSession(world.publisher_id,"invalid",world.clock_id),"SessionDenied");eq(x.r.snapshot()["lifecycle"],"Configured");eq(x.r.snapshot()["last_fault"],null) } }
    run("integrity-valid noncanonical association flag fails closed") { val path=Files.createTempDirectory(output,"flag-").resolve("association.bin");Rig(path).use { it.live() };val raw=Files.readAllBytes(path);raw[12]=2;Files.write(path,raw.copyOf(raw.size-32)+hash(raw.copyOf(raw.size-32)));ReceiverRuntime().use { eq(it.configure(ReceiverConfig("127.0.0.1",32123,path)),"ConfigurationRejected");eq(it.start(),"StartDenied") } }
    run("corrupt receiver history marker fails closed") { val path=Files.createTempDirectory(output,"marker-").resolve("association.bin");Rig(path).use { it.live() };Files.write(path.resolveSibling("association.bin.order-required"),byteArrayOf(2));ReceiverRuntime().use { eq(it.configure(ReceiverConfig("127.0.0.1",32123,path)),"ConfigurationRejected");eq(it.start(),"StartDenied") } }
    run("normal rebind metadata path cannot clear association") { Rig().use { x ->x.setup();eq(x.r.restart(x.config.copy(associationPath=x.path.resolveSibling("other.bin"))),"RestartDenied");eq(x.r.snapshot()["retained_association"],assoc.view());ck(!Files.exists(x.path.resolveSibling("other.bin"))) } }
    for(point in listOf("decode","apply")) run("shutdown during pending $point rejects completion") {
        val entered=CountDownLatch(1);val release=CountDownLatch(1);val hook={entered.countDown();check(release.await(5,TimeUnit.SECONDS))}
        Rig(hooks=if(point=="decode")ReceiverHooks(beforeDecode=hook) else ReceiverHooks(beforeApply=hook)).use { x ->
            x.setup();x.fake.queue.put(wireBytes(world));ck(entered.await(5,TimeUnit.SECONDS));val done=CountDownLatch(1)
            val t=thread { x.r.close();done.countDown() };x.r.await {it["lifecycle"]=="Stopping"};release.countDown();ck(done.await(5,TimeUnit.SECONDS));t.join();eq(count(x.r.snapshot(),"accepted"),0L);eq(count(x.r.snapshot(),"stale_socket_completion"),1L)
        }
    }
    run("restart during completion cannot make old socket current") {
        val entered=CountDownLatch(1);val release=CountDownLatch(1);val f1=Fake();val f2=Fake();var factories=0
        ReceiverRuntime({if(factories++==0)f1 else f2},ReceiverHooks(beforeApply={entered.countDown();check(release.await(5,TimeUnit.SECONDS))})).use { r ->
            eq(r.configure(ReceiverConfig("127.0.0.1",32123,Files.createTempDirectory(output,"race-").resolve("association.bin"))),"Configured");eq(r.associate(assoc),"Associated");eq(r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");eq(r.start(),"Receiving")
            f1.queue.put(wireBytes(world));ck(entered.await(5,TimeUnit.SECONDS));var answer="";val t=thread {answer=r.restart()};r.await {it["lifecycle"]=="Stopping"};release.countDown();t.join(5000);ck(!t.isAlive);eq(answer,"Receiving");eq(count(r.snapshot(),"accepted"),0L);eq(count(r.snapshot(),"stale_socket_completion"),1L)
            f2.queue.put(wireBytes(world.copy(source_publisher_id="source-publisher-B")));r.await {count(it,"rejected")==2L};eq(reason(r.snapshot()),"SourcePublisherMismatch")
        }
    }
    run("deterministic receive error closes socket and trust") { Rig().use { x ->x.live();x.fake.queue.put(java.io.IOException("SYNTHETIC_SECRET"));x.r.await {it["lifecycle"]=="Faulted"};eq(x.r.snapshot()["last_fault"],"ReceiveFailed");eq(x.r.snapshot()["world_current"],false);ck(!Gson().toJson(x.r.snapshot()).contains("SYNTHETIC_SECRET")) } }
    run("bind failure observable and preserves retained tuple") { val path=Files.createTempDirectory(output,"bind-").resolve("association.bin");ReceiverRuntime({throw BindException("SYNTHETIC_SECRET")}).use { r ->eq(r.configure(ReceiverConfig("127.0.0.1",32123,path)),"Configured");eq(r.associate(assoc),"Associated");eq(r.start(),"BindFailed");eq(r.snapshot()["last_fault"],"BindFailed");eq(r.snapshot()["retained_association"],assoc.view());ck(!Gson().toJson(r.snapshot()).contains("SYNTHETIC_SECRET")) } }
    run("bind failure after successful run retains association and highwater") { val f=Fake();var calls=0;ReceiverRuntime({if(calls++==0)f else throw BindException("isolated")}).use { r ->eq(r.configure(ReceiverConfig("127.0.0.1",32123,Files.createTempDirectory(output,"second-bind-").resolve("association.bin"))),"Configured");eq(r.associate(assoc),"Associated");eq(r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");eq(r.start(),"Receiving");f.queue.put(wireBytes(world));r.await {count(it,"accepted")==1L};eq(r.restart(),"BindFailed");eq(r.snapshot()["retained_association"],assoc.view());eq(r.snapshot()["accepted_world_revision_high_water"],world.world.coordinate_space.revision);eq(r.snapshot()["world_current"],false) } }
    run("network snapshot boundaries build identity and reasons") { Rig().use { x ->x.live();x.send(world.copy(source_publisher_id="source-publisher-B"));val s=x.r.snapshot();eq(s["bound_ipv4"],"127.0.0.1");eq(s["bound_port"],32123);eq(s["required_wire_version"],"monaka-wire-2.2");eq(s["last_accepted_wire_version"],"monaka-wire-2.2");eq(count(s,"DatagramReceived"),3L);eq(count(s,"WireDecoded"),3L);eq(count(s,"AssociationValidated"),2L);eq(count(s,"StateApplied"),2L);eq(reason(s),"SourcePublisherMismatch");ck(!Gson().toJson(s).contains(x.path.toString())) } }
    run("strict bounded local config control rejects secrets duplicate keys") { failure { strictObject("{\"a\":1,\"a\":2}") };failure { strictObject("{\"SYNTHETIC_SECRET\":\""+"x".repeat(17000)+"\"}") };ck(true) }
    run("REAL isolated IPv4 socket A B same sender endpoint identity gate") {
        val channel=DatagramChannel.open(StandardProtocolFamily.INET);channel.bind(InetSocketAddress("127.0.0.1",0));val endpoint=channel.localAddress as InetSocketAddress
        ReceiverRuntime({Ipv4ReceiverSocket.owned(channel)}).use { r ->
            eq(r.configure(ReceiverConfig("127.0.0.1",endpoint.port,Files.createTempDirectory(output,"udp-").resolve("association.bin"))),"Configured");eq(r.associate(assoc),"Associated");eq(r.authorizePublisherSession(world.publisher_id,world.session_id,world.clock_id),"SessionAuthorized");eq(r.start(),"Receiving")
            DatagramChannel.open(StandardProtocolFamily.INET).use { sender ->
                sender.bind(InetSocketAddress("127.0.0.1",0));eq(sender.send(ByteBuffer.wrap(wireBytes(world)),endpoint),wireBytes(world).size);r.await {count(it,"accepted")==1L};val before=state(r.snapshot())
                val b=world.copy(source_publisher_id="source-publisher-B");eq(sender.send(ByteBuffer.wrap(wireBytes(b)),endpoint),wireBytes(b).size);r.await {count(it,"rejected")==1L};eq(reason(r.snapshot()),"SourcePublisherMismatch");eq(state(r.snapshot()),before)
                sender.send(ByteBuffer.wrap(wireBytes(world)),endpoint);r.await {count(it,"rejected")==2L};eq(reason(r.snapshot()),"duplicate")
                sender.send(ByteBuffer.wrap(ByteArray(6000)),endpoint);r.await {count(it,"rejected")==3L};eq(reason(r.snapshot()),"DatagramLength")
            };eq(r.stop(),"Stopped");eq(r.snapshot()["retained_association"],assoc.view())
        }
    }
    run("REAL production binder exclusive failure and clean shutdown") {
        DatagramChannel.open(StandardProtocolFamily.INET).use { occupied ->
            occupied.bind(InetSocketAddress("127.0.0.1",0));val endpoint=occupied.localAddress as InetSocketAddress
            ReceiverRuntime().use { r ->eq(r.configure(ReceiverConfig("127.0.0.1",endpoint.port,Files.createTempDirectory(output,"occupied-").resolve("association.bin"))),"Configured");eq(r.associate(assoc),"Associated");eq(r.start(),"BindFailed");eq(r.snapshot()["last_socket_error"],"BindException");eq(r.snapshot()["retained_association"],assoc.view()) }
        }
        val lease=DatagramChannel.open(StandardProtocolFamily.INET);lease.bind(InetSocketAddress("127.0.0.1",0));val port=(lease.localAddress as InetSocketAddress).port;lease.close()
        ReceiverRuntime().use { r ->eq(r.configure(ReceiverConfig("127.0.0.1",port,Files.createTempDirectory(output,"production-binder-").resolve("association.bin"))),"Configured");eq(r.start(),"Receiving");eq(r.snapshot()["bound_port"],port);eq(r.stop(),"Stopped");eq(r.start(),"Receiving");eq(r.stop(),"Stopped") }
    }
    ck(cases>=50)
    val result=mapOf("result" to "PASS","cases" to cases,"assertions" to assertions,"real_production_endpoint_connections" to 0,"isolated_ipv4_socket_tests" to true,"source_producer_changes" to false,"receiver_runtime" to true)
    Files.writeString(root.resolve("receiver/build/receiver-tests.json"),Gson().toJson(result)+"\n")
    println("PASS receiver $cases cases / $assertions assertions; actual production codec/Core and isolated IPv4 only")
}
