# Bridge Q-jump implementation and focused evidence

2026-11-07, OFFLINE. Owner: root-dispatched GPT-6.1 Sol / High (`qjump_bridge_owner`), sole Bridge implementation owner. No child agents. Base `29d24bd4c06ece0bab59235803dace621fa6c3dc`; topic `diagnostic/qjump-capture-20261107`, worktree `H:/tmp/6Dof/MonakaBridge`. HEAD remains the base until root G2/independent review permits the local topic commit. Scope is diagnostic observation and software fixtures only.

## Product path

`Bridge::receive` invokes a default-empty, exception-isolated observer only after actual Registry acceptance; it passes the original accepted `Device.rawPose` and exact receipt `now`. Existing canonical decode/admission/normalization still runs once. Unknown profiles are recorded as `transform_status: unknown_profile`; no transform is fabricated and no diagnostic drop is charged for this ordinary blocked-output state.

`calibrate` optionally copies its actual signed-axis Q, previous history Q and sign-selected Q. It retains the existing arithmetic/order and history mutation. The fresh/new-pose block observes the actual calibrated value and the actual post-output-normalization return of `publish`. The service's existing send operation is called once; its success and encoded MVIP envelope are observed afterward only for the Monaka channel. Queue publication and `actual_wire` have separate lanes/phases. Sending successfully is not consumer admission.

`QJumpBridge` records actual upstream Observation identity, exact decimal integer timestamps/session/sequence, mapping revision, immutable signed-axis/world/mount/profile snapshots, and named `links.input`/`links.observation` tuples. Output identity adds the actual Bridge publisher/logical tracker/session/clock/sequence. Original source clocks, Bridge service-epoch derived/publication times, collector process steady clock and UTC are labelled separately. No input publisher/body target is invented. RuntimeNormalization's existing replay observer is untouched.

## Collector, profile and limits

The generic header `include/monaka_bridge/qjump_capture.hpp` is copied by the separate producer implementation with provenance; it is not a cross-repository runtime dependency. Shared candidate SHA256: `118673c94ae252334f84bd939ffa02e35ccbd871c5d926a5653458a43c8cae38`.

Default OFF. Enable via:

```powershell
$env:BRIDGE_QJUMP_DIAGNOSTICS = '1'
$env:BRIDGE_QJUMP_DIAGNOSTICS_DIR = '<absolute output directory>'
$env:QJUMP_CAPTURE_COORDINATION_DIR = '<absolute common diagnostic directory>'
# Optional common diagnostic step threshold; never a rejection threshold:
$env:QJUMP_CAPTURE_STEP_DEGREES = '45'
```

An output run subdirectory is created with component/process UUID. Enable requires both explicit absolute output and coordination directories. Normal warmup is at least 10 s. Remove the enable variable to roll back to default-off on the next separately authorized normal launch. This task does not launch products or change persisted product settings.

Production defaults: 20 Hz retained samples per lane, 10 s pre/post, full accepted-sample-cadence sign-invariant 45° T1 detection, fresh >=2 s T2 persistence, T6 session/clock/space/mapping/transform/policy transitions, and generic remote T1–T6 requests. Reset/assignment/proxy semantics remain consumer-owned. Recovery clears the original offset reference. Duplicate/regressing source timestamps cannot prove persistence. First observations seed lifecycle state. Exact before/after fast-trigger samples are retained in the bundle even between regular frames.

Tracking-thread work is bounded JSON/scalar construction, validation and a nonblocking move into a fixed atomic SPSC queue; no filesystem access, event history traversal or event serialization occurs there. Collection worker owns detection, ring and request polling; separate writer owns large bundle encoding and durable fflush/_commit/write/close/rename. IO/exception failures become diagnostic counters. Regular status is at most 1 Hz; coordination polls at most every 100 ms under ordinary nonblocked operation.

Explicit storage bounds:

- 512 queued sample objects, exactly one service-thread producer and one collector-worker consumer (remote requests are worker-owned); each has <=256 metadata nodes, depth <=8, bounded strings/keys/arrays and conservative <=12,000-byte metadata encoding estimate before ownership transfer. Exported rows are capped at 16 KiB.
- <=16 source/entity pairs, <=256 lanes; each lane has at most three bounded latest/history sample objects. Input device and logical output identity are separately counted.
- Ring <=51,200 rows and <=16 MiB; samples are also evicted by the 10 s prewindow, retaining its preceding anchor where count/byte caps permit.
- One active capture, one pending writer event, one in-flight writer event. Each event including encoded metadata is <=32 MiB; max 128 markers/aliases, 16 events/run, <=512 MiB event output/process/run. Retention stops new capture with EXHAUSTED status; earlier evidence is preserved.
- Request files <=4096 bytes; <=256 inspected directory entries and <=256 remembered names. Only direct regular UUID-named JSON files are read; requests have no control/path action. Remote requests are not rebroadcast. Their original type/origin/UTC plus local receipt are retained.

These are object/encoded storage limits, not a measured RSS ceiling. Slow writes can stall a writer and eventually exhaust the bounded pending queue, with explicit dropped/error counters. An ordinary shutdown flushes an incomplete active event where possible; final join can wait on an OS file operation. No tracking loop waits for disk during operation.

Schema is `qjump.capture.v1` JSONL header/sample/trigger/footer. Footer `complete` describes capture-window closure, warmed prehistory and no drop/error/truncation/shutdown. `post_data_present` and `no_new_post_samples` separately report actual postwindow data; a silent participant can close its capture window without inventing new/fresh poses. Analyzer cause classification must require the appropriate actual data coverage. UTC coverage bounds come from actual samples; capture-window UTC bounds are separate. Local steady timestamps are never cross-process clocks. Diagnostic status/errors conservatively prevent later complete claims in the run after evidence loss.

## Focused validation

Toolchain: installed Visual Studio 18 2026 CMake/MSVC 19.51, x64 Release. Isolated `build-qjump`, GUI/SteamVR OFF for focused core tests. Configure verified existing pinned kits through normal CMake checks; no kit/schema edits. Sandbox MSBuild stalled before compilation for the core; canceled only the task's own session and repeated the approved focused build with `require_escalated` and `/nr:false`, which compiled normally. No unrelated process cleanup.

Commands:

```powershell
cmake -S . -B build-qjump -A x64 -DBUILD_TESTING=ON -DMB_BUILD_STEAMVR=OFF -DMB_BUILD_CONTROL_GUI=OFF
cmake --build build-qjump --config Release --parallel 2 --target qjump_bridge_tests qjump_capture_tests monaka_bridge_service -- /nr:false
build-qjump/Release/qjump_capture_tests.exe build-qjump/qjump-capture-evidence
build-qjump/Release/qjump_bridge_tests.exe build-qjump/qjump-bridge-evidence
```

`qjump_capture_tests`: PASS D1 normal motion, D2 q/-q no trigger, D3 120°, D4 180°, D5 recovered fast step with exact before/after and no false reverse-offset persistence, D6 persistent step and native-nonprogress negative, D7–D9 simultaneous real-context categories retained, D10 session/space change, D11 five-entity ring/event caps, oversize/drop accounting and blocked/failing writer while offers progress. Silent remote T4 request completes capture without new upstream poses. These are software diagnostic fixtures; Bridge does not own actual Reset/assignment/proxy operations. Final generic run output in tool transcript: `PASS D1-D11 collector: sign/step/recovery/persistence/lifecycle/silent-request/bounds/writer isolation`.

`qjump_bridge_tests`: PASS actual Bridge receive/calibrate/publish/fanout OFF/ON exact encoded output equality, all Bridge stage lineage, immutable transforms, queued vs actual-wire send result, exception-isolated hooks, and admitted unknown-profile/no transformed output/zero diagnostic-drop negative. Final persisted stdout: `build-qjump/qjump-bridge-final.log`.

Actual product-hook bundle example:

`build-qjump/connected-accepted/bridge-3debe2ed-2770-4228-8536-9c6241d35273/2f75f012-ba85-4199-8882-0e3fcb36e779.jsonl`

This is generated by real software hooks, not authored sample records. Short focused fixture durations are explicitly in its header; it does not establish full-duration physical capture. Latest recorded status for this fixture has zero drops/errors.

Focused test binary SHA256:

- `build-qjump/Release/qjump_capture_tests.exe`: `699026dc201b4ce139abb46c2e28713c762c6d020090969ba9a565335d169701`.
- `build-qjump/Release/qjump_bridge_tests.exe`: `f51b6f6d37faf921274d81b0895bf4ed7369446e57bb719f1151f3168b461a88` (final focused build; normal runtime provenance remains the release build authority).

## Gate / remaining work

Sol implementation result: focused software checks PASS; separate PICO-owner review ACCEPT after retention/shutdown fixes, root G2 specification/responsibility review ACCEPT. Topic commit and normal fixed runtime build are authorized after the final focused PASS; overall candidate state remains partial until build provenance and coordinated consumer/analyzer evidence are recorded by root. `PHYSICAL_HIL=NOT_RUN`, `CUTOVER=NOT_RUN`.

The connected fixture mode accepts exact actual producer Observation JSONL and emits real Bridge MVIP JSONL for the consumer test, with explicit generic identity transforms and actual codec/model path. It is an offline test, no sockets or hardware. Exact consumed input snapshot is `build-qjump/connected-sealed/input-observation.jsonl`; real emitted MVIP is `build-qjump/connected-mvip-accepted.jsonl`; command stdout is `build-qjump/qjump-connected-accepted.log`. The bundle above has common incident `7a9ec380-55f1-4a85-9b9a-645ab15a8d01`, footer aliases, complete/post_data_present true and zero drops/errors. Connected fixture explicitly uses 20 Hz retention, 100 ms pre and 1 s post. It tests exact lineage through genuine codecs/hooks; sequential replay has different real UTC windows and must yield the analyzer window-negative result, not an artificially synchronized cause claim. No broad regression or historical byte audit was run. Root has granted local topic commit and one normal fixed runtime build after final focused PASS. No product start/deploy authority is added.

Rollback source is the base commit above; default-off requires no product correction or migration. Source/controller/runtime behavior changes are excluded: no spike rejection, calibration correction, heading freeze, automatic Reset, assignment/IK change, vendor-private access, producer lifecycle control, HIL, push or merge.

## Nominal capture and queue rework acceptance

The original try-lock sample queue lost evidence under nominal 15-sample bursts even without capacity pressure. This was REWORK, not an accepted tracking/capture result. Root approved the smaller actual single-producer design: fixed atomic head/tail slots with release/acquire ownership; no mutex contention on offers. Genuine queue-full/oversize failures remain explicit losses. Shared footer aliases were added to conform to the analyzer contract; 64 KiB is reserved for bounded header/footer metadata including maximum aliases.

After those repairs, the same shared header passed D1–D11 and real Bridge parity/unknown-profile/footer-alias tests again. Commands/logs: `build-qjump/Release/qjump_capture_tests.exe build-qjump/qjump-capture-final` -> `build-qjump/qjump-capture-final.log`; `build-qjump/Release/qjump_bridge_tests.exe build-qjump/qjump-bridge-final` -> `build-qjump/qjump-bridge-final.log`.

Real-time nominal command: `build-qjump/Release/qjump_capture_tests.exe --nominal build-qjump/qjump-nominal-final`. This uses actual default 20 Hz, 10 s pre + 10 s post, five opaque entities x three stage lanes, and 15 offers per 50 ms burst from one producer while the worker collects concurrently. PASS: all offers accepted, dropped=0, writer_errors=0, exactly one durable event, complete=true and post_data_present=true. Measured encoded ring maximum 1,069,414 bytes; event storage maximum 2,197,560 bytes. This is internal encoded storage, not RSS. Log `build-qjump/qjump-nominal-final.log`; actual bundle `build-qjump/qjump-nominal-final/bridge-f02e24d2-ce0f-4735-859f-280ad0117367/cae98a16-41f8-4648-84dc-73fbfb977fea.jsonl`.

The shorter 1 ms/30 ms/1000 Hz parity fixture is separately labelled software timing and is not the nominal acceptance above. Fixture build identity is explicitly a fixture or unavailable; the normal service supplies the generated P1 Bridge source SHA/input hash and pinned MBIP/MVIP identifiers to collector initialization. The final normal build adds its ordinary clean-source/binary provenance after root G2. No special seal or fixture identity substitutes for that provenance.

Final independent review repairs: reaching the final allowed event now reports EXHAUSTED immediately while its postwindow/merged markers continue. Writer-stop predicate is changed under the same mutex used by its condition-variable wait, preventing a lost shutdown wake. Focused max_events=1 retained both step/recovery markers and one event; 20 immediate start/stop trials passed. Final affected D1–D11 and actual Bridge parity rerun logs: `build-qjump/qjump-capture-review-final.log`, `build-qjump/qjump-bridge-review-final.log`.

The final nominal load is **five entities x five Bridge lanes = 25 lanes** (input, axis, calibrated, published queued, published actual_wire), 25 offers per 50 ms burst. This supersedes the earlier 15-lane measurement for Bridge load scope. Actual default 20 Hz/10 s + 10 s PASS, zero drops/errors, one completed artifact; encoded ring maximum **1,841,060 bytes**, event maximum **3,779,325 bytes**. Log: `build-qjump/qjump-nominal25-final.log`; bundle: `build-qjump/qjump-nominal25-final/bridge-05eafe75-0ea0-43db-8d9d-b5b6f200b00f/2d78ad67-0d4f-4ace-8300-c5f4d0283da8.jsonl`. The earlier 15-lane result is retained as earlier evidence, not claimed to cover all Bridge lanes.
