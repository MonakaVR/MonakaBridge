package dev.monaka.tracking
import com.google.gson.GsonBuilder
import dev.monaka.protocol.v2.*
import java.io.File

/** Test-only scaffolding around the unmodified frozen Core comparison below. */
object FrozenCoreProbe {
    private data class Message(val capabilities: List<String>)
    private class Device { var capabilityConflict=false; var capabilities: DeclaredTrackerCapabilities?=null; var sample: Any?=null; var context: Any?=null }
    private class Inbox { var conflicts=0; fun count(reason: String) { check(reason=="CapabilityConflict"); conflicts++ } }
    private data class Key(val observationId: String)
    @JvmStatic fun main(args: Array<String>) {
        val device=Device(); val removed=mutableSetOf<String>(); val inbox=Inbox(); val key=Key("bounded-single-logical-tracker")
        fun admit(pose: Message?, state: Message?) {
            // BEGIN exact frozen MtpObservationBackend capability block
val declaration = DeclaredTrackerCapabilities(pose?.capabilities ?: state!!.capabilities)
		if (device.capabilityConflict || (device.capabilities != null && device.capabilities != declaration)) {
			device.capabilityConflict = true
			device.sample = null; device.context = null; removed += key.observationId
			inbox.count("CapabilityConflict"); return
		}
		device.capabilities = declaration
            // END exact frozen block
        }
        for (name in args) {
            val decoded=MonakaCodec.decodeEnvelope(File(name).readBytes()) as DecodeResult.Success
            when(val m=decoded.value) {
                is MtpPose -> admit(Message(m.capabilities),null)
                is MtpTrackerState -> admit(null,Message(m.capabilities))
                else -> error("Core probe only accepts MTP; no source-type inference")
            }
        }
        println(GsonBuilder().serializeNulls().create().toJson(mapOf("conflict" to device.capabilityConflict,"conflicts" to inbox.conflicts)))
    }
}
