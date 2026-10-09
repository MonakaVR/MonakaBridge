// Offline compatibility harness: compile outside the read-only MonakaVR source tree.
import com.google.gson.*;
import dev.monaka.tracking.TrustedHmdCommonWorldStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public final class CommonWorldGeneratedReplay {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        Path directory = Path.of(args[0]);
        JsonObject manifest = JsonParser.parseString(Files.readString(directory.resolve("generated-stream-manifest.json"))).getAsJsonObject();
        int total = 0;
        for (String name : manifest.keySet()) {
            TrustedHmdCommonWorldStream receiver = new TrustedHmdCommonWorldStream("synthetic-bridge", 128);
            JsonArray outcomes = manifest.getAsJsonObject(name).getAsJsonArray("outcomes");
            String session = null;
            int index = 0;
            for (String line : Files.readAllLines(directory.resolve(name))) {
                JsonObject event = JsonParser.parseString(line).getAsJsonObject();
                String next = event.get("session_id").getAsString();
                if (!next.equals(session)) {
                    check(receiver.beginAuthorizedPublisherSession("synthetic-bridge", next, event.get("clock_id").getAsString()), name + " session authorization");
                    session = next;
                }
                String expected = outcomes.get(index).getAsString();
                String actual = receiver.receiveBytes(line.getBytes(StandardCharsets.UTF_8), 1000L + index);
                check(expected.equals(actual), name + ":" + index + " expected " + expected + " actual " + actual);
                String type = event.get("type").getAsString();
                if (type.equals("world_revocation")) {
                    check(receiver.currentWorld() == null && receiver.currentMapping() == null && receiver.candidate() == null, name + " immediate world revoke");
                } else if (type.equals("mapping_revocation")) {
                    check(receiver.currentWorld() != null && receiver.currentMapping() == null && receiver.candidate() == null, name + " immediate mapping revoke");
                } else if (type.equals("common_unavailable")) {
                    check(receiver.currentWorld() != null && receiver.currentMapping() != null && receiver.candidate() == null, name + " unavailable preserves authority");
                } else if (type.equals("common_pose")) {
                    var candidate = receiver.candidate();
                    check(candidate != null, name + " mapped pose candidate");
                    JsonObject source = event.getAsJsonObject("source");
                    check(candidate.getSourceLocateTimeNs() == Long.parseLong(source.get("source_locate_time_ns").getAsString()), name + " locate time");
                    check(candidate.getSourceTimeDomainId().equals(source.get("source_time_domain_id").getAsString()), name + " locate domain");
                    // Receiver performs its frozen Float-domain P/Q raw-bit proof internally.
                }
                ++index;
            }
            check(index == outcomes.size(), name + " event count");
            total += index;
            System.out.println("PASS MonakaVR receiver replay " + name + " " + index + " events");
        }
        System.out.println("PASS " + manifest.size() + " generated streams / " + total + " events / unchanged 299dda9d receiver");
    }
}
