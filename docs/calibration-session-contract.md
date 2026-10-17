# B2 local calibration contract (software policy 1)

Base: accepted B1 `7ac9ed9fe4a19391a1f88ed4d00adaefbdbbd64c`, stacked on
dev `279a01363f47ad3564b5cd8b9346180c49ee41d3`. R2 D1-D4 are adopted.
This contract is fixed before implementation. B1 FrozenMeasurementTarget is reused.
Production capture is UNSUPPORTED in B2; injected providers link only into tests.
No WPF/OpenVR/mirror/physical/product calibration completion claim.

## State and operations

One worker/session/run. Phases: NEW -> OPEN -> DISCOVERED -> CONFIRMED ->
RECORDING -> SOLVING -> RESULT_READY -> PREPARED -> COMMITTING -> SAVED or
SAVED_CONFIG_EVIDENCE_STALE. CANCELLED/CLOSED/FAILED/INSPECT_ONLY/UNKNOWN
are terminal for new live work. Precommit cancel/EOF closes without config writes.
Committing cancel/EOF cannot promise rollback or CANCELLED; actual outcome wins.
Exception/timeout/lost response after transaction entry is conservatively UNKNOWN.

`--local-api-version` and `--local-api` dispatch before legacy ParseArgs/VR_Init.
Version/open do not initialize VR, bind UDP, or create drafts. Legacy CLI bodies
and semantics remain byte exact. No production replay/bypass/test flags.

NDJSON version `1`, strict UTF-8/duplicate keys/types/unknown fields. Every request
has `api_version`, `request_id`, `op`; after open, `session_id`/`run_id` are exact.
open explicitly supplies config_path, source/device/tracker/input_space/revision,
readonly, helper identity and operator-declared association/reference selection.
No inferred paths or automatic v2 profile upgrade. Core ops: open, discover,
confirm_target, begin, capture_point, cancel, prepare_commit, commit, close.
capture_point is UNSUPPORTED in B2. Native draft-management methods implement
quota/retention/bounded explicit cleanup; product draft-management UI is B3/B4.

Native confirmation binds original config bytes/path/hash, tool identity,
operator-declared association, B1 group, source session/clock/space, reference
serial/role/connection-generation/Standing, pose floor and optional state floor.
Initial floors never change (including absent state floor). Moving counters and
128 canonical-content entries per pose/state stream are separate. <= highwater
is stale/duplicate; same-sequence different content in ring invalidates. State
never renews pose freshness. Other sources are ignored boundedly; selected peer,
lifetime/frame/scope conflicts invalidate. This is not sender authentication.

Confirm needs source/reference fresh <=50ms and valid, no absence/conflict.
After begin, invalid/lost samples count toward quality. Explicit absence,
identity/frame/config change, or fresh-valid receipt gap >=1s invalidate sticky.
Review stale >50ms blocks actions transiently, fresh same lifetime can recover
until sticky/TTL invalidation. Before/after solve and before prepare/commit drain
guard frames without changing frozen solve arrays/result. B3 must connect all
actual selected envelopes and reference events to these contracts.

## Fixed bounds

| Resource | Bound |
|---|---|
| IDs/cache | 128 distinct IDs: ordinary126 + reserved cancel/close2; no eviction; canonical request64KiB, outcome16KiB, combined10MiB |
| Queue | ordinary8 + reserved cancel/close2; overflow BUSY before action |
| Parser | line64KiB, depth16, fields/object64/total256, array256, string4KiB/path32KiB; 16 protocol rejections close precommit |
| Deadlines (monotonic) | discover10s, confirm-to-begin10s, solve10s, result-review10min, prepare30s, worker20min; equal deadline expires |
| Shutdown/commit | precommit grace2s; commit15s; post-entry uncertainty UNKNOWN; no automatic retry |
| Events | progress4Hz/latest1; controls7 + terminal1; stderr256KiB/run, journal4MiB/run |
| Draft | owner LocalAppData/MonakaBridge/CalibrationDrafts only; begin/export/manage explicit creation; run64MiB/32files, raw48MiB, expected/candidate2MiB each, metadata2MiB |
| Root | 16 run dirs/512MiB, free>=256MiB, short exclusive quota lock/cooperating reservations |
| Retention/cleanup | 30d expired inspect-only still counted; explicit max4runs/128files/256MiB; known manifest paths only, no alias/reparse, active/UNKNOWN excluded from automatic deletion |

Progress candidate event sequences may have gaps after coalescing. Each emitted
event has previous_emitted_sequence. Accept only sequence>last and previous==last;
reverse/duplicate is ignored and unexplained chain mismatch fails protocol.
Cached outcome replay gets a new event sequence with replayed=true but original
tokens/deadlines; current phase/eligibility accompany it. Pipe failure cannot
guarantee terminal delivery. Native result/terminal artifacts precede best-effort
response; parent treats missing commit response as UNKNOWN/inspect-only.

## Same-result commit

Native immutable TrajectoryResult is SoT. Solve once; capture arrays end once.
prepare uses existing worldCalibrationCandidate/saveConfig at a new private path,
then freezes exact bytes. Only selected shared group W and approval=false change;
revision+1 once, E/M/profile/decl/world_revision/other groups/unknown JSON retained.
Review responds with native result ID, candidate path/size/SHA and opaque review
token (candidate may be2MiB; cached outcome remains <=16KiB). Reprepare retains
exact candidate bytes and replaces token; replay never renews it.
commit accepts IDs/hash/token, never GUI W/E or recomputation input. It rechecks
frozen config/lifetime/freshness/deadlines and private artifact bytes, then calls
existing commitConfigDocument once with original bytes and native candidate.
capture/solve at commit=0. Readonly rejects in native handler. Saved response is
SAVED CONFIG / OUTPUT REAPPROVAL REQUIRED / RUNTIME UNCONFIRMED. Restart/import
cannot reconstruct committable authority from files/hash/mtime. No same-user
OS-security claim. No live CAS lock held during discovery/capture/review.

## Acceptance allocation

T01-10 native parser/state/scope/witness/reference fixtures; T11 fixture association,
bounded backlog/multi-source only (actual bind/ownership B3); T12 existing solver
negatives + native fixture quality; T13-24 native draft/result/candidate, CAS,
fault/cancel/EOF/event/liveness fixtures and actual isolated process; T25 static
UNSUPPORTED + inherited legacy math (full static B4); T26 exact source/pin/provider
link separation and actual executable bootstrap (product packaging B3); T27 saved
config vs runtime/output remains explicit. R07-R17, native portions R18/R22 apply.
Actual mirror/OpenVR/WPF/v2 profile, OS visual, C01, physical/HIL remain unconnected.

## B2 independent-review corrections

The B2 recording boundary records locally stale (>50ms receipt age) source
samples as invalid quality attempts, even when the wire observation says tracked.
Each historical reference sample independently requires a nonfuture local sample
time and age<=50ms; the latest batch reference flag governs current liveness,
never blanket validity of historical samples. Legacy recorder/solver unchanged.

Absolute worker20min expiry is checked before cache replay and on every stdio
worker iteration, including a nonempty queue. Cached outcomes cannot renew it.
A known SAVED/SAVED_CONFIG_EVIDENCE_STALE/UNKNOWN historical response remains
historical after expiry, with eligible=false and worker_lifetime_expired=true;
new work is rejected. Actual entered commit keeps its actual-save/UNKNOWN rules.

The machine API is one dedicated process with owned inherited byte pipes (not a
console/file transport). There is exactly one reader and no other pending IO on
its input handle. It peeks and reads only available bytes in <=4096-byte chunks,
checking stop even while draining partial/oversized lines. No std::cin buffering.
Pipe mode is not mutated: mode changes can fail while parent reads are pending.
Writer cancellation is latched and repeated during the existing2s shutdown grace,
closing cancellation-before-WriteFile races. Join occurs only after the actual
thread handle is signalled. If the writer cannot finish by that absolute deadline,
the dedicated worker terminates itself with exit3; no detached IO or unbounded
join. An undelivered commit response remains UNKNOWN to the parent; no rollback
or automatic retry is implied. Other processes are never terminated by this API.

Private candidate serializer staging, both candidate/lock hashes and manifest
publication share the root quota lock. All cooperating root scans use that lock.
Contention returns DRAFT_QUOTA_BUSY and can be retried; it does not classify a
cooperating writer's incomplete publication as corruption. Live config CAS lock
is not held. A crashed partial draft remains inspect-only. Draft replacements
charge old+staging bytes and transient file count, with2MiB manifest reserve;
oversized48MiB raw rewrite rejects before temporary allocation.

Native-only ioHook/draftPublishHook barriers are library test injection only,
never CLI/JSON features of the production calibrator. Test process provider and
fault flags remain linked only into calibration_session_process_fixture.
