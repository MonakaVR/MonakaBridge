# Bridge Q-jump diagnostic acceptance

2026-11-07 / OFFLINE. Requirement: user-approved residual-twist capture task and root `MonakaVR/docs/diagnostics/qjump/QJUMP_CAPTURE_DESIGN.md` including the event-close boundary amendment. Owner: root-dispatched GPT-6.1 Sol / High (`qjump_bridge_owner`), sole Bridge implementation owner, no child agents. Worktree `H:/tmp/6Dof/MonakaBridge`; topic `diagnostic/qjump-capture-20261107`; rollback/base `29d24bd4c06ece0bab59235803dace621fa6c3dc`.

Separate PICO-owner implementation/header review: **ACCEPT**. Root specification/responsibility review and final original-footer inspection: **ACCEPT**. Final header bytecopy SHA256: `c75546f75f7586b4e128aa28293c4d604a144407b4e48f32edcdc2de9aece86c`. The nominal/window measurement below was run on `cc04a2e29881b6b0bc021550102e8e7f908761debf3c05b54eb6c7baa249dfde`; the final difference is the independently reviewed and focused-tested T2-only repair described below. Final topic HEAD and normal build provenance are reported by root; no amend/rebase/push/dev integration or product startup is authorized by this document.

## Product path and unchanged tracking

`Bridge::receive` observes the original accepted `Device.rawPose` and exact receipt `now` after normal Registry admission. Existing decode, authorization, replay/session/capability decisions and normalization still execute once. Unknown configured profiles record `transform_status: unknown_profile` without fabricating a transform or charging an ordinary blocked-output state as diagnostic loss.

An optional `CalibrationDiagnostic` copies the **actual** signed-axis Q, previous history Q and sign-selected Q inside the existing `calibrate` call. Arithmetic, history and world/mount application are unchanged. The fresh/new-pose block copies actual calibrated Q and the actual post-normalization return of `publish`. RuntimeNormalization's replay observer remains untouched.

`QJumpBridge` installs exception-isolated default-off hooks. The normal service's existing send executes once, then observes its encoded MVIP and send result. `publication_phase: queued` and `actual_wire` are separate lanes; a successful send is not consumer admission. Both normal headless/tray and native-owner paths use `include/monaka_bridge/service_runtime.hpp::runService`.

Input identities contain only actual Observation source/device/session/clock/sequence/space fields. Published identities add actual Bridge publisher/logical tracker/session/clock/sequence/mapping revision. `links.input` and `links.observation` contain the same actual upstream four-tuple. No input publisher or body target is invented. Decimal strings preserve integer timestamps, sequences, revisions and counters. Source clocks, exact receipt, Bridge service-epoch derived/publication times, recorder steady clock and UTC remain distinct. Each transformation sample includes immutable profile axes, world/mount values, spaces/revisions and routing policy.

## Runtime profile and storage

Default OFF. Explicit opt-in requires both absolute directories:

```powershell
$env:BRIDGE_QJUMP_DIAGNOSTICS = '1'
$env:BRIDGE_QJUMP_DIAGNOSTICS_DIR = '<absolute output directory>'
$env:QJUMP_CAPTURE_COORDINATION_DIR = '<absolute common diagnostic directory>'
# Optional common diagnostic threshold, never a rejection threshold:
$env:QJUMP_CAPTURE_STEP_DEGREES = '45'
```

Warm up for at least 10 s after a separately authorized normal launch. A new component/process run directory is used. Removing the enable variable restores default-off on the next separately authorized launch; this task never starts products or rewrites product settings.

Defaults: 20 Hz regular retention per lane, 10 s pre + 10 s post, sign-invariant >=45 degree T1 at actual accepted-sample cadence, fresh/progressing >=2 s T2, and T6 source/session/clock/space/mapping/transform/policy changes. Recovery clears the original offset reference. Unchanged/regressing source timestamps cannot prove persistence. Consumer Reset/assignment/proxy events remain consumer-owned generic diagnostic requests, not fabricated local generations.

The tracking service is the sole sample producer. It performs bounded immutable construction/validation and moves into a fixed SPSC queue; no tracking-thread filesystem IO, event traversal or large serialization. A collector worker owns detection, ring and <=100 ms request polling. A separate writer encodes and durably flushes/closes bundles. Normal status writes are at most 1 Hz. Diagnostic exceptions/failures do not change tracking.

Limits:

- 512 queued samples; <=256 metadata nodes, depth <=8, bounded keys/strings/arrays and conservative <=12,000-byte metadata estimate before ownership transfer; exported rows <=16 KiB.
- <=16 source/entity pairs and <=256 lanes, each with at most three bounded latest/history objects. Input and logical-output identities count separately.
- Ring <=51,200 rows and <=16 MiB; one active capture, one pending writer event and one in-flight writer event. Events <=32 MiB including metadata, <=128 markers/aliases, <=16 events/run and <=512 MiB event output. EXHAUSTED is set on the last accepted event while its own postwindow/markers continue.
- Direct regular UUID-named requests <=4096 bytes, <=256 inspected entries and remembered names. No paths/control actions or rebroadcast. Original request type/origin/UTC and local receipt are retained.

These are object/encoded bounds, not an RSS or real-device latency claim. Writer backlog and actual capacity loss are explicit counters. Ordinary operation never waits on disk in the tracking thread; final collector destruction can wait for an OS file operation. Writer stop uses the same mutex as its condition-variable predicate.

At event close, the worker includes each lane's latest **actual** sample if regular thinning has not saved it, including invalid samples, with original Q/identity/mono/UTC/validity. Boundary records are labelled and counted against the existing event cap. Rows are sorted by actual monotonic observation time; footer exports actual per-lane bounds. No heartbeat or new pose is synthesized. Precoverage is frozen from in-window ring data at the trigger, so old dormant data cannot manufacture warmup. Post validity requires an actually fresh valid Q near the deadline; silent/invalid-only tails stay insufficient. Metadata reservation is bounded to min(half the event cap, 256 KiB); insufficient metadata/content space explicitly truncates evidence.

Schema remains additive `qjump.capture.v1`. A closed `complete` capture window and `post_data_present` are separate facts; analysis must inspect both. No normalizer, rejection threshold, calibration correction, heading clamp, automatic Reset, assignment or IK change was introduced.

## Focused commands and evidence

Installed Visual Studio 18 2026 CMake/MSVC 19.51, x64 Release; isolated `build-qjump`, SteamVR/GUI OFF for focused core tests. CMake verified existing pinned protocol/MBIP kits. Sandbox MSBuild stalled before compilation; the approved isolated retry with `require_escalated` and `/nr:false` succeeded. No unrelated processes were stopped.

```text
cmake -S . -B build-qjump -A x64 -DBUILD_TESTING=ON -DMB_BUILD_STEAMVR=OFF -DMB_BUILD_CONTROL_GUI=OFF
cmake --build build-qjump --config Release --parallel 2 --target qjump_capture_tests qjump_bridge_tests -- /nr:false
build-qjump/Release/qjump_capture_tests.exe build-qjump/qjump-final-coverage-focused
build-qjump/Release/qjump_bridge_tests.exe build-qjump/qjump-bridge-final-boundary
build-qjump/Release/qjump_capture_tests.exe --nominal build-qjump/qjump-nominal25-final-coverage
```

PASS D1–D11 applicable to the shared collector: smooth motion/sign equivalence; 120/180 degree and recovered between-retention steps with exact before/after; persistent step and nonprogress/silence negatives; simultaneous lifecycle markers; session/space; five entities, byte/count/queue/retention bounds and failing/stalled writer. Extra focused checks cover silent remote capture, original invalid boundary Q/time, cold/dormant precoverage, max_events=1 marker survival, and twenty immediate shutdowns. Logs: `build-qjump/qjump-final-coverage-focused.log`, `build-qjump/qjump-bridge-final-boundary.log`.

Actual Bridge hooks PASS: OFF/ON exact encoded output equality through receive/calibrate/publish/fanout; input/axis/calibrated/queued/wire lineage, transform snapshots, callback exceptions and admitted unknown-profile/no-output/zero-drop negative. No broad regression or historical byte audit was rerun.

Measured cc04 real-time nominal (not relabelled as a c755 run): **five entities x all five Bridge lanes**, 25 offers every 50 ms, actual default 20 Hz retention and 10+10 s window, offers continuing for 21.2 s. The test reads the original durable footer and asserts complete/pre/post true, no_new false, drops/errors zero, one bounded file. PASS. Encoded ring max **1,841,090 bytes**; event storage max **3,750,090 bytes**; actual file **3,757,509 bytes**. Original footer independently inspected by root and PICO reviewer, with 25 actual boundary rows, 25 lane coverage entries and chronological samples.

Log: `build-qjump/qjump-nominal25-final-coverage.log`.
Bundle: `build-qjump/qjump-nominal25-final-coverage/bridge-50ee6d99-3783-4d15-8adb-58d1fb2ef4bd/11e08ad8-a0f3-4c51-8dc2-858c9329e3ff.jsonl`.

Earlier 25-lane captures had complete windows but `post_data_present=false`; the owner's initial post-PASS report was incorrect and withdrawn. Their original artifacts/logs remain preserved. The diagnostic boundary repair and explicit original-footer assertions above supersede that result; no predicate was weakened. Intermediate commit `ae2187e3327bd7b484da9067b08eb378a050f2aa` and its normal build are not the final accepted candidate.

Recorded cc04 isolated focused binary SHA256: collector `4c0254509798c095631cc3b698ef82057d8809b9a6e62d37b86744c26a5297d2`; Bridge `e6fbc40b27fab73b8184dfed3eb4cabb7ad83ed8d85d8547363418525553b569`. Final normal-build provenance, rather than these test binaries, is the runtime release authority.

## Actual-format connected evidence and remaining gates

Final matching producer generation: `PicoMotionTrackerBridge/Build/qjump-tests/connected-v4/producer-published.jsonl` and `pico-85cf7700-d67b-4b5b-823e-2241fcb64e6b/b1014052-fefd-46e3-8a3d-8b9ff024de62.jsonl` under the same v4 directory. Bridge consumes that actual sent/received Observation wire through its real codec/product path, explicitly configured with generic identity axes/world/mount. Exact consumed snapshot: `build-qjump/connected-v4/input-observation.jsonl`; actual MVIP: `build-qjump/connected-v4-mvip.jsonl`; log: `build-qjump/qjump-connected-v4.log`.

Actual Bridge bundle: `build-qjump/connected-v4/bridge-e7e234a2-2a63-4178-89dd-fb0f32518644/fd859841-501f-481a-8bd6-9d4adcf89e72.jsonl`. Original footer complete/pre/post true and zero drops/errors; shared incident `7a9ec380-55f1-4a85-9b9a-645ab15a8d01`. The fixture declares 20 Hz, 100 ms pre and 1 s post; it does not replace the default-window nominal evidence. All source/session/sequence/timestamps remain exact. Sequential producer/Bridge/consumer replay has different real UTC windows and must remain a strict window-negative analysis; synchronized CASE A–E fixtures are a separate analysis test, not invented concurrency.

Normal service initialization supplies generated actual P1 Bridge source SHA/input hash and pinned MBIP/MVIP identities. Fixture headers explicitly identify fixtures or unavailable build identity; they do not establish runtime provenance. After the follow-up approved clean topic commit, `python scripts/build_runtime.py` builds only fixed `Build/runtime` and records `Build/provenance/windows.json`; no launch/install/register is performed. Final commit/clean/build outcome is supplied to root.

Bridge focused software and separate implementation/specification review: PASS/ACCEPT. Coordinated final acceptance and remaining Monaka/analyzer verification are root-owned. `PHYSICAL_HIL=NOT_RUN`, `CUTOVER=NOT_RUN`; no real rare-event capture or new physical stability claim.

## Final T2-only impact acceptance

Root G2 reopened one diagnostic semantic defect: clearing the original anchor on every short duplicate prevented T2 when genuine native updates were interleaved with faster duplicate receipts. The final c755 header preserves the anchor during short duplicates, tracks an actual source/sequence high-water mark and last-progress time in bounded scalar lane state, and fires T2 only on the currently progressing fresh sample after two seconds. Regressions never lower high water. A >250 ms progress/observation gap, invalidity, recovery or lifecycle/session change clears the candidate; stalled time is not credited on resume. Native explicit `persistence_fresh` and sequence-only observations are supported without inventing absent fields. Tracking, sampling, ring/event/queue/storage limits and close-boundary handling are unchanged.

Focused command: `build-qjump/Release/qjump_capture_tests.exe --t2-progress build-qjump/qjump-t2-progress-final`; log `build-qjump/qjump-t2-progress-final.log`. PASS nine default-two-second scenarios: mixed fresh/duplicate positive using source timestamp, native explicit progress flag and sequence-only lineage; duplicate-only, regression, stalled-resume gap, invalidity, recovery and session negatives. Trigger records assert firing only on the progressing sample. PICO reviewer independently accepted the diff and copied/rebuilt/tested the same c755 source. Root accepted this limited delta and authorized an ordinary follow-up commit and normal fixed build.

The cc04 25-lane 20-second raw capacity/window artifact remains the actual measurement for that unchanged path; it is not a new-source rerun. The final-source validation is that measurement plus unchanged-sampling/capacity diff review and the focused T2 impact evidence. No unrelated suite or 20-second repeat was run for this scalar-state correction. Final runtime SHA/source authority remains the ordinary `Build/provenance/windows.json` produced from the clean follow-up commit.
