"""Compile only the exact frozen capability equality check, with bounded test scaffolding."""
import argparse,hashlib,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
lock=json.loads((R/'tests/evidence.lock.json').read_text())
for n,h in lock['files_sha256'].items():assert hashlib.sha256((R/n).read_bytes()).hexdigest()==h,n
declaration=(R/'tests/reference/core/TrackerInputCapabilities.kt').read_text(encoding='utf-8').split('/** Persisted user selection.')[0].rstrip()+'\n'
source=(R/'tests/reference/core/MtpObservationBackend.kt').read_text(encoding='utf-8')
begin=source.index('val declaration = DeclaredTrackerCapabilities(');last='device.capabilities = declaration';end=source.index(last,begin)+len(last)
snippet=source[begin:end]
scaffold='''package dev.monaka.tracking
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
SNIPPET
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
'''.replace('SNIPPET',snippet)
for name,data in [('FrozenDeclaration.kt',declaration),('FrozenCoreProbe.kt',scaffold)]:
 out=R/'jvm/src/test/kotlin/dev/monaka/tracking'/name
 if a.check:assert out.read_text(encoding='utf-8')==data,name
 else:out.parent.mkdir(parents=True,exist_ok=True);out.write_text(data,encoding='utf-8',newline='\n')
print('PASS exact frozen Core set/equality block + generated scaffolding drift')
