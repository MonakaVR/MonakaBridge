package dev.monaka.tracking

import com.google.gson.*
import com.google.gson.stream.*
import java.io.StringReader
import java.nio.file.Files
import java.nio.file.Path
import java.util.Properties

internal fun receiverBuild(): Map<String,String> {
    val p=Properties();object {}.javaClass.getResourceAsStream("/build-identity.properties")?.use { p.load(it) }
    return p.stringPropertyNames().associateWith { p.getProperty(it) }
}
/** Local control/config JSON only. All datagrams go exclusively through MonakaCodec. */
internal fun strictObject(text: String): JsonObject {
    require(text.toByteArray(Charsets.UTF_8).size<=16384)
    val reader=JsonReader(StringReader(text));reader.strictness=Strictness.STRICT
    fun value(depth: Int): JsonElement {
        require(depth<=16)
        return when(reader.peek()) {
            JsonToken.BEGIN_OBJECT -> { val o=JsonObject();reader.beginObject();while(reader.hasNext()) { val k=reader.nextName();require(!o.has(k));o.add(k,value(depth+1)) };reader.endObject();o }
            JsonToken.BEGIN_ARRAY -> { val a=JsonArray();reader.beginArray();while(reader.hasNext())a.add(value(depth+1));reader.endArray();a }
            JsonToken.STRING -> JsonPrimitive(reader.nextString())
            JsonToken.NUMBER -> JsonPrimitive(java.math.BigDecimal(reader.nextString()))
            JsonToken.BOOLEAN -> JsonPrimitive(reader.nextBoolean())
            JsonToken.NULL -> { reader.nextNull();JsonNull.INSTANCE }
            else -> error("InvalidJson")
        }
    }
    val v=value(0);require(reader.peek()==JsonToken.END_DOCUMENT && v.isJsonObject);return v.asJsonObject
}
private fun JsonObject.only(vararg keys: String) { require(keySet()==keys.toSet()) }
private fun JsonObject.string(key: String): String { val v=get(key);require(v!=null && v.isJsonPrimitive && v.asJsonPrimitive.isString);return v.asString }
internal fun readReceiverConfig(p: Path): ReceiverConfig {
    require(Files.size(p)<=16384);val j=strictObject(Files.readString(p));j.only("schema","enabled","required_wire","bind_ipv4","udp_port","association_path")
    require(j.string("schema")=="monaka-common-world-receiver-config-1")
    require(j["enabled"].isJsonPrimitive && j["enabled"].asJsonPrimitive.isBoolean && !j["enabled"].asBoolean)
    val port=j["udp_port"];require(port.isJsonPrimitive && port.asJsonPrimitive.isNumber)
    return ReceiverConfig(j.string("bind_ipv4"),port.asBigDecimal.toBigIntegerExact().intValueExact(),Path.of(j.string("association_path")),j.string("required_wire")).also { it.validate() }
}
private fun line(): String? {
    val b=java.io.ByteArrayOutputStream()
    while(true) {
        val c=System.`in`.read();if(c<0) { require(b.size()==0);return null }
        if(c==10) return Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
            .decode(java.nio.ByteBuffer.wrap(b.toByteArray())).toString()
        require(b.size()<16384);b.write(c)
    }
}
fun main(args: Array<String>) {
    val gson=GsonBuilder().serializeNulls().create()
    fun emit(o: Any)=println(gson.toJson(o))
    ReceiverRuntime().use { r ->
        if(args.isEmpty()) { emit(r.snapshot());return }
        if(args.size!=2 || args[0]!="--control") { emit(mapOf("result" to "InvalidArguments"));return }
        try {
            val config=readReceiverConfig(Path.of(args[1]).toAbsolutePath())
            if(r.configure(config)!="Configured") { emit(mapOf("result" to "ConfigurationRejected","snapshot" to r.snapshot()));return }
            emit(mapOf("event" to "ConfiguredInactive","snapshot" to r.snapshot()))
            while(true) {
                val raw=line() ?: break
                try {
                    val j=strictObject(raw);val operation=j.string("operation")
                    val result=when(operation) {
                        "status" -> { j.only("operation");"Snapshot" }
                        "associate" -> { j.only("operation","owner_id","world_space_id","source_publisher_id");r.associate(Association(j.string("owner_id"),j.string("world_space_id"),j.string("source_publisher_id"))) }
                        "authorize-session" -> { j.only("operation","publisher_id","session_id","clock_id");r.authorizePublisherSession(j.string("publisher_id"),j.string("session_id"),j.string("clock_id")) }
                        "start" -> { j.only("operation");r.start() }
                        "stop" -> { j.only("operation");r.stop() }
                        "restart" -> { j.only("operation");r.restart() }
                        "rebind" -> { j.only("operation","bind_ipv4","udp_port");r.restart(config.copy(bindIpv4=j.string("bind_ipv4"),port=j["udp_port"].asBigDecimal.toBigIntegerExact().intValueExact())) }
                        "shutdown" -> { j.only("operation");r.close();"Shutdown" }
                        else -> "InvalidCommand"
                    }
                    emit(mapOf("result" to result,"snapshot" to r.snapshot()));if(operation=="shutdown")break
                } catch(_: Throwable) { emit(mapOf("result" to "InvalidCommand","snapshot" to r.snapshot())) }
            }
        } catch(_: Throwable) { emit(mapOf("result" to "InvalidConfiguration","snapshot" to r.snapshot())) }
    }
}
