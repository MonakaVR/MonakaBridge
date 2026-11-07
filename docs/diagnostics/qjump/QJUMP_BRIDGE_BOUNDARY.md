# Q-jump Bridge boundary audit

Date: 2026-11-07. Phase: OFFLINE / read-only production audit.

| Record | Value |
|---|---|
| Task | QJUMP-BRIDGE-G0; boundary audit and later sole Bridge diagnostic owner |
| Specification | supplied `MonakaVR_QJump_ResidualTwist_捕捉診断_Offline_Orchestration_Codex指示_20261107.md`, §§4,6,8–14; root and repository AGENTS.md |
| Requested / actual worker | GPT-6.1 Sol / High, root-dispatched worker; no further spawn |
| Worktree / inspected branch | `H:/tmp/6Dof/MonakaBridge` / `dev` |
| Base / inspected HEAD | `29d24bd4c06ece0bab59235803dace621fa6c3dc` |
| Initial state | clean; no production edits, build, application control or commit performed |
| Allowed write at this gate | this audit document only |
| Acceptance | source-path and field evidence sufficient to freeze capture design; implementation awaits root G0 |
| Software / PHYSICAL_HIL / CUTOVER | `NOT_RUN` / `NOT_RUN` / `NOT_RUN` |
| Sol audit disposition | `ACCEPT` as boundary inventory only; capture implementation `NOT_RUN` |
| Astra specification review | pending root review |

## Canonical product route and available facts

`runService` (`include/monaka_bridge/service_runtime.hpp`) is shared by the headless/tray child and native owner. It receives at most 128 datagrams per loop through `DiscoveryCache::receive`, then ticks and flushes the Bridge, with a 1 ms sleep. All Bridge state remains owner-thread state. Discovery predecode is diagnostic-only and still unconditionally invokes the original `Bridge::receive`.

`Registry::receive` (`src/observation/registry.cpp`) decodes the actual pinned codec, performs authorization/session/peer/sequence/space/capability admission, and retains:

- `Device.rawPose`: accepted original `TrackerObservation`, before capability normalization.
- `Device.pose`: admitted `TrackerObservation`, after runtime normalization.
- `Device.poseSequence`, `fixedTime`, source session/clock and `Source.lastReceive`.
- `fixedTime = receipt_now - (sent_at_ns - timestamp_ns)` when the age fits the local clock; it is the age-adjusted Bridge sample time, **not exact receipt time**. `Source.lastReceive` is source-wide and can subsequently be updated by another device/state packet. Capture exact pose receipt at the original receive call when required.

Canonical input `TrackerObservation` has `source_id`, `device_id`, `session_id`, `clock_id`, `sequence`, `timestamp_ns`, `sent_at_ns`, `timestamp_kind`, coordinate space, modality, validity, tracking state, orientation evidence and capabilities. It has **no publisher_id, tracker_id or body target**. Do not invent those upstream identities. Bridge-owned mapping supplies logical tracker ID; output supplies Bridge publisher ID. No Reset/assignment/proxy generation exists in this layer.

`Bridge::tick` (`src/routing/bridge.cpp`) has accepted input, binding, profile, history and actual generated output together. The fresh/new-sample block calls `calibrate`, applies declared output component capabilities, sets validity/modality, sets `MtpPose.input`, and stores the return of `publish` into `Logical.pose`. `publish` applies canonical output normalization and queues the same normalized envelope for Monaka and SteamVR according to policy.

Published `MtpPose` includes canonical `publisher_id`, `source_id`, `tracker_id`, output `session_id`/`clock_id`, output `sequence`, output coordinate space/revision, `mapping_revision`, plus `input.source_id/device_id/session_id/sequence/orientation_evidence`. Its timestamp is the Bridge `fixedTime`, kind `receive`; its `sent_at_ns` initially equals publish `now`. `Fanout::flush` later updates output `sent_at_ns` to flush time. Therefore the post-normalizer value is a **publication/queue observation**, not proof of socket transmission or consumer admission; if exact wire send timestamp is required, capture it at fanout flush. Consumer wire keys still match publisher/source/tracker/session/sequence and input provenance.

## Quaternion transformations and proposed capture hooks

`src/calibration/calibration.cpp` performs:

1. when orientation is present/valid and modality is not `none`, `q = normalized(permute(input.orientation, profile.quaternionAxes))`;
2. derivative calculation and sign continuity against `History.q`;
3. `calibratedQ = normalized(world.rotation * q * mount.rotation)`;
4. history stores the signed-axis/sign-selected q and current fixed sample time.

History changes quaternion representation and supports angular velocity; no accumulated heading correction is present. It is cleared for missing Q, stale input, session/frame changes and reconfiguration. Diagnostics must not alter it.

Minimal hooks after design approval:

- Capture immutable values around the existing fresh/new-sample block: accepted raw Q, admitted input Q, signed-axis Q before continuity, selected Q after continuity, calibrated Q, and post-output-normalization MVIP Q. An optional diagnostic observation parameter/result from `calibrate` avoids running a second independently reconstructed transform as if it were the actual intermediate value.
- Snapshot configured profile quaternion/position axes, profile convention, world and mount rotation/translation, input/world space/revision, config mapping revision, timeout and routing policy. Transform values must accompany the samples/config transition they describe; an event-final snapshot alone can misdescribe pre-event samples if configuration changed mid-window.
- Record null Q and validity explicitly; stale/missing observations must not be represented as newly measured poses. Keep original sample sequence/time when the 20 Hz ring repeats a latest sample, and label repeat/freshness.
- Pump sampling/post-window completion from the service loop even with no packets. Otherwise input silence can prevent capture completion and a downstream lifecycle trigger cannot freeze upstream history.
- Generic opt-in diagnostic request coordination must remain separate from tracking wire and producer control. It can request diagnostic snapshots only; it cannot admit input, change calibration/output, or start/stop/configure a producer.

## Reuse and limitations

`RuntimeNormalization::observer` exposes before/after full envelopes, but is explicitly documented as replay-only and is currently empty in live service. Its 64 recent records report capability declaration normalization, not a timed pose history. Do not install a live synchronous serializer through this callback without reviewing failure isolation and hot-path cost. Existing `--replay` (`tools/runtime_replay.cpp`) exercises real `Bridge::receive -> Registry -> normalization -> calibrate -> publish -> fanout`, creates no sockets, refuses existing output and preserves before/after normalization evidence. It is useful for OFFLINE product-path equivalence.

`healthSnapshot` (`src/routing/health.cpp`) describes native/admitted observation, explicitly `is_final_output=false`. It runs every 500 ms and lacks calibrated/published Q and full wire timestamps. It cannot substitute for the ring. `HealthWriter::publish` supplies a reusable pattern: one pending immutable value, `try_lock`, old-value destruction outside lock, serialization/write/flush/atomic replace on a background thread, counters and failure isolation. Its latest-only replacement semantics are unsuitable for preserving multiple incident bundles; a separate strictly bounded event queue/retention policy is needed.

Recommended initial rate is 20 Hz, pre/post 10 s, with sign-invariant >=45° step and >=2 s persistent-offset diagnostic triggers. Check trigger deltas at actual observation cadence so a short recovered spike between 20 Hz samples is not silently lost; preserve trigger-adjacent samples separately under a fixed cap if root design adopts that choice. Session/space, mapping/profile/world/mount and policy transitions are available. Reset/assignment/proxy triggers arrive only as generic diagnostic coordination metadata and must not become local invented lifecycle generations.

Use separate local process monotonic and wall UTC time with declared clock domains. Current service `now` is monotonic since service epoch; input timestamp domain belongs to input `clock_id`, while output timestamp/clock belongs to Bridge session. Never subtract monotonic clocks across processes. Diagnostic event ID is separate from canonical tracking identity.

## Test and build plan (not executed)

After root-approved schema/hooks, add focused C++ tests over actual Bridge input/output, not only ring unit tests. Existing `tests/helpers.hpp`, `bridge_tests.cpp`, `runtime_contract_tests.cpp`, `rotation_only_downstream_tests.cpp`, `runtime_normalization_process.py` and replay provide fixtures and product-path checks.

- D1–D6: normal motion, q/-q equivalence, 120°/180° steps, recovery with full pre/post, >=2 s persistent offset. Compare normalized tracking outputs with diagnostics OFF/ON to prove no tracking change. Include an event wholly between periodic samples.
- D7–D9: generic incoming consumer diagnostic request must preserve/freeze the Bridge ring even when no new upstream packets arrive; actual reset/assignment/proxy generation semantics remain consumer-owned. Do not claim Bridge locally owns these lifecycle values.
- D10: accepted new session and space revision produce diagnostic events; rejected replay/unknown profiles preserve existing admission behavior.
- D11: five synthetic opaque identities, equivalent policies/transforms, bounded sample/event/request/artifact sizes, blocked/failing dump IO and malformed requests cannot stall or fail tracking; no IO in the tracking thread. Use `health_tests.cpp`'s blocked-IO technique.
- Regressions: `bridge_core`, `runtime_contract`, `rotation_only_downstream`, coordinate/profile/calibration/mapping tests and capability normalization process tests; exact CTest names to inspect before running. Architecture checks must include two synthetic distinct opaque producer identities and no private producer dependency.

Isolated test builds may use CMake with `BUILD_TESTING=ON`, separate build output, and SteamVR OFF where appropriate; do not use `Build/runtime` as test scratch. `scripts/test_revision.ps1 -Mode Quick` documents the repository regression lane, but some release-tooling checks are historical and do not grant modern runtime acceptance. Normal fixed runtime is built **only** via `python scripts/build_runtime.py`, after clean topic commit and root release of running-product locks. It uses `Build/runtime`, normal CMake x64 Release, GUI + SteamVR, no startup/install/register side effect, and creates source/binary provenance. Do not rebuild now while the fixed runtime is running.

Current actual CMake links the hash-pinned canonical MBIP P1 kit (`dependencies/mbip-p1.lock.json`, source `3ea3194479cfa1a04b0fcbcf04916186efb4f9d3`) and MVIP v2.2 codec (`dependencies/monaka-protocol-v2.2.lock.json`, source `0b4a27c2e2de88705c92fb6dbf5a8d9ca752bf39`). Historical Task4 fixed-v1 examples do not authorize wire/schema changes. Full kit integrity commands and binary SHA belong to implementation/build evidence; they were not rerun for this read-only audit.

## Remaining design decisions / preservation

Root must freeze shared schema, request transport/validation/bounds, event merge/cooldown, retention/disk caps, overflow accounting and wall-clock-correlation tolerance. A permanently slow filesystem can delay worker shutdown; tracking-loop isolation and shutdown boundedness should be stated separately. Events lacking an upstream capture must remain partial evidence rather than cause proof.

No correction, rejection threshold, heading freeze, reset, assignment recovery, wire addition, configuration write, producer-private process access, launch, HIL or cutover is authorized through this document. Rollback at this gate is omission of this additive audit file; production remains at the inspected base.
