# Phase 2B-5AB: dormant source authority receiver

`TrustedHmdSourceStream` is compiled into `monaka_bridge_core` and is never instantiated by
`bridge_service`. The caller serializes mutations and explicitly authorizes one expected publisher,
fresh publisher session and clock with `beginAuthorizedPublisherSession`. Decoding is not authorization.
There is no listener, socket, thread, source-to-common transform, allocator or publisher in this change.

## Exact offline dependency

Protocol source: `ea1e02c15c20693070ce69cfbc54941ee7ae8e7d`.

| Receipt | SHA256 |
|---|---|
| monaka-protocol-kit-v2.1.zip | 8d9ba5087cf9935d7c2432962086d305b34e71d805838bd96ffbef9eef90413d |
| handoff-manifest.json | 9d71b722da0e5a6f72d82cc2373069d570e59c195a43b58e539b29827caa14b8 |
| protocol.lock.json | e196692dbe3c533fdd93e18e73c24a660cbefc88780802b10119c1dfc3398e55 |

The new pin is `dependencies/monaka-protocol-v2.1.lock.json` and its exact artifact/manifest/lock
bytes are in `dependencies/protocol-v2.1`. CMake invokes `scripts/verify_protocol_v21.py`.
The verifier is read-only: it checks CRC, safe unique paths (including case-fold collisions), full
SHA256SUMS coverage, lock provenance, wire 2.1, C2/C2.1, every current member and absence of stale files.
`third_party/monaka-protocol-v2` is the complete 298-file materialization, including the supplied JAR.
The historical pin and `dependencies/protocol-v2` remain unchanged;
`scripts/verify_protocol_v2.py` independently verifies that v2.0 receipt without touching the current tree.

## Lifecycle

Only SourceAuthority, SourcePose, SourceUnavailable and SourceRevocation are accepted. Other decoded
messages return `wrong-protocol` without mutation. The existing latest-only fanout separately rejects
all ten authority types; existing MTP publication still explicitly emits version 2.0.

Fresh publisher sessions start at seq0 SourceAuthority. All subsequent accepted stream events must be
contiguous across the four types. Duplicate equality compares typed immutable headers and payload,
excluding only `sent_at_ns`. Binary64 pose bits and quaternion signs are retained in this comparison.
JSON order/whitespace and codec-discarded optional fields are irrelevant. Duplicate/replay events do
not update candidate receipt time. Lower sequence wins over changed clock/content and cannot invalidate
good current state. Same-last-sequence conflict, changed current clock, gap, malformed authorized bytes
or transport disconnect immediately clear authority/candidate and close that session permanently.
No reorder buffer exists. At INT64_MAX only duplicate/replay ignores are possible; no successor wraps.

Publisher sessions and source-token records each default to **128** entries, configurable to a positive
bound. All selected publisher sessions are retained, including the current one. No entry is evicted;
capacity exhaustion closes the receiver permanently. A new session cannot reuse any selected session ID.

Source records retain the exact publication, generation high-water, generation/token retirement and
last immutable observation across publisher replacement. Same live authority facts may bootstrap again;
lower/retired generations cannot. Changed facts at the same generation retire that generation.
The source locate-clock domain cannot change under one source token. Selecting a different source token
retires the previous selected token even across publisher replacement. Proven higher generation under a
nonretired token may follow `source_space_ambiguous` revocation. `source_session_loss` and
`event_stream_lost` retire the entire token, prohibiting all future generations of it.

Pose source ID/token/space/generation/time domain must match exactly. Observation ID is independent of
stream sequence and remains monotonic across generations/session replacement. Lower observation ignores;
equal exact source/evidence ignores; equal changed facts revoke the generation and clear the candidate.
Unavailable clears only the candidate. Recovery requires a new matching observation; repeating the old
observation cannot undo unavailable. Revocation clears authority/candidate immediately with no next pose.
Transport disconnect is local invalidation, never a fabricated wire revocation; fresh session/bootstrap
is required, while source histories remain intact.

The snapshot contains source wire facts and first local receipt only. It is not a fresh/trusted runtime
sample, and no unrelated clocks are subtracted. Replay persistence across process restart remains future work.

## Evidence and production gate

Tests consume the exact kit JSONL bytes through the supplied decoder and this receiver, plus additional
authorization, high-water, unavailable, clock, conflict/gap, capacity and protocol-isolation cases.
The 5AA test-only oracle calls a repeated observation at a newer stream sequence `pose`; 5AB reports
`duplicate-observation` and asserts the existing structural candidate and unchanged receipt instead.
Kit bytes are never rewritten to match the test. Full existing registry/calibration/mapping/MTP/Direct/
GUI/UDP and codec regressions are required separately.

Live ALVR sender, Bridge common owner/calibration/publisher, authority transport, Reviewed registration,
Strong Trusted, RawHmdPoseInput production and Position Correction remain blocked/not enabled.
No installed driver/config changes, 5S deployment or HIL are part of this phase.

Next phase: **Phase 2B-5AC — MonakaBridge Common World Runtime Owner / Dormant Publisher Foundation**.
