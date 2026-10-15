package dev.monaka.bridgeinput

import com.google.gson.GsonBuilder
import com.google.gson.JsonParser
import dev.monaka.input.*
import dev.monaka.protocol.v2.*
import java.io.File

object NormalizerRunner {
    @JvmStatic fun main(args: Array<String>) {
        try {
            val b = JsonParser.parseString(File(args[0]).readText()).asJsonObject
            val id = b.getAsJsonObject("identity")
            val binding = Binding(Identity(id["family"].asString,id["publisher"].asString,id["source"].asString,id["entity"].asString),
                b["session"].asString, CapabilitySet(b.getAsJsonArray("declaration").map { it.asString }), b["provenance"]?.asString ?: "")
            val mode = Mode.entries.firstOrNull { it.name == args[2] } ?: throw ContractError("UnsupportedMode")
            val normalizer = Normalizer(binding, mode)
            val d = MonakaCodec.decodeEnvelope(File(args[1]).readBytes())
            if (d is DecodeResult.Failure) { println("ERROR:${d.code}"); return }
            val result = normalizer.normalize((d as DecodeResult.Success).value)
            val encoded = MonakaCodec.encodeEnvelope(result.message) as EncodeResult.Success
            val channels = result.channels.mapValues { (_, c) ->
                mapOf("supported" to c.supported,"sample_present" to c.sample_present,"valid" to c.valid,"state" to c.state)
            }
            println(GsonBuilder().serializeNulls().create().toJson(mapOf("channels" to channels,
                "wire" to JsonParser.parseString(encoded.value.toString(Charsets.UTF_8)))))
        } catch (e: ContractError) { println("ERROR:${e.code}") }
    }
}
