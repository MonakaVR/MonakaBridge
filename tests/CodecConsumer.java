import java.nio.file.*;
import dev.monaka.protocol.v1.*;
// Test harness only. All validation/serialization remains in the supplied fixed JAR.
public class CodecConsumer {
 public static void main(String[] args) throws Exception {
  DecodeResult d=MonakaCodec.decodeEnvelope(Files.readAllBytes(Path.of(args[0])));
  if(d instanceof DecodeResult.Failure){System.out.println("ERROR:"+((DecodeResult.Failure)d).getCode());return;}
  EncodeResult e=MonakaCodec.encodeEnvelope(((DecodeResult.Success)d).getValue());
  if(e instanceof EncodeResult.Failure)throw new AssertionError(e);
  System.out.write(((EncodeResult.Success)e).getValue());
 }
}
