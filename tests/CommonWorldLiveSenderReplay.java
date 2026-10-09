// Offline test only. The unchanged receiver intentionally rejects implicit world replacement.
import com.google.gson.*;
import dev.monaka.tracking.TrustedHmdCommonWorldStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public final class CommonWorldLiveSenderReplay {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        CommonWorldGeneratedReplay.main(args); // normal and same-owner transport reconnect
        var receiver = new TrustedHmdCommonWorldStream("synthetic-bridge", 128);
        String[] expected = {"world", "mapping", "pose", "untrusted", "untrusted", "untrusted"};
        String session = null;
        int index = 0;
        for (String line : Files.readAllLines(Path.of(args[0]).resolve("sender-owner-handover.jsonl"))) {
            var event = JsonParser.parseString(line).getAsJsonObject();
            String next = event.get("session_id").getAsString();
            if (!next.equals(session)) {
                check(receiver.beginAuthorizedPublisherSession("synthetic-bridge", next, event.get("clock_id").getAsString()), "explicit test session approval");
                session = next;
            }
            String actual = receiver.receiveBytes(line.getBytes(StandardCharsets.UTF_8), 1000L + index);
            check(index < expected.length && expected[index].equals(actual), "handover " + index + " actual " + actual);
            if (index >= 3) check(receiver.candidate() == null, "implicit replacement must never admit a pose");
            ++index;
        }
        check(index == expected.length, "handover frame count");
        System.out.println("PASS 1 negative sender owner-handover stream / 6 events / unchanged receiver rejects implicit replacement; local acceptance is not application delivery");
    }
}
