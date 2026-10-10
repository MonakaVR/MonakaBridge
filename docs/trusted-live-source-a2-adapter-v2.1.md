# Phase 2B-5AI-S: ALVR OpenXR trusted live source

This phase implements one Windows producer: the ALVR OpenXR VIEW-in-STAGE
accepted-evidence exporter, a production-envelope converter, and a continuous A2
adapter. Default startup is inert. It does not approve 5AH-E2 or 5AH-B, install a
driver, start ALVR/SteamVR, pair devices, or choose a final endpoint.

## Source and dependencies

The ALVR fork is `https://github.com/orzkwsk/ALVR.git`, phase branch
`feature/trusted-live-source-authority-export-v2.1`. Its starting authority
implementation is `e94ff1967c70da480531c3ec9f6956d3be8b5ce5`, descended from
upstream `e0d83b46168449cb0bb514770dd926b0ed85c55a`. Bridge starts at receiver R2
`a2a5df9640ffff5dcfac67faf845cc880286142f`; Protocol is
`0b4a27c2e2de88705c92fb6dbf5a8d9ca752bf39` and the fixed 2.2 kit SHA256 is
`14f51d889ce665ee1f598bc67edacf0e7bc70d16ce0af9f1788c5f1efee04e85`.

ALVR is the smallest candidate because its Android OpenXR implementation already
captures immutable source pose, locate evaluation time, six validity facts,
observation identity, source token and reference generation. The server checks
the exact single HEAD binding before any recenter/prediction. PICO and VIVE have
real Observation paths but lack that complete trusted HMD authority/time/frame
contract. The generic OpenVR path needs additional raw-space/mapping authority.
Replay and synthetic fixtures supply no real tracking source.

Real use requires the matching authority-capable ALVR OpenXR Android client from
this source lineage, a runtime supporting VIEW and STAGE, a compatible ALVR
streamer/SteamVR/OpenVR runtime, Windows x64, and the operator's explicit local
configuration. The artifact includes the rebuilt Windows ALVR driver and its
runtime libraries; it does not install or configure those dependencies. The
Android client and physical runtime behavior require the subsequent environment
and HIL phases. Software tests do not claim device validation.

## Canonical identity and configuration

An operator registers one opaque, case-sensitive `source_publisher_id`. It is
stored in an external producer JSON config and the A2 activation config, and in
the R2 association tuple `(owner_id, world_space_id, source_publisher_id)`.
Production codec validation requires nonblank valid UTF-8, 1..96 encoded bytes,
and no forbidden controls. No case folding or normalization is applied. The ID
persists across application, pipe, transport and OpenXR restarts until an
operator explicitly changes the registration. A new installation must not
silently reuse another installation's identity. The inactive template leaves all
deployment IDs and pipe name empty; it is intentionally unusable until registered.
Tests use `source-publisher-A` and `source-publisher-B` explicitly.

In the existing `monaka.hmd_authority` wire 2.1 source envelope this canonical ID
is the `publisher_id` field. The unchanged A2/store/publisher path preserves it
as `source_publisher_id` in `monaka.common_world_identity` wire 2.2. The output
`publisher_id` remains Runtime Owner identity. These fields are not substituted
for each other. The source ID and source-space ID are separately configured,
stable operator registrations for the HEAD and VIEW-in-STAGE stream. They do not
pretend to be a hardware serial number.

The exporter is disabled without an explicit process environment variable
`MONAKA_ALVR_AUTHORITY_PIPE=\\.\pipe\<registered-name>`. Only local ASCII
alphanumeric, hyphen and underscore names (1..96 characters) are accepted.
The converter requires `--run <external-config>` and the adapter requires
`--run`, explicit producer/runtime binaries, both configs and an external
emergency-stop path. Config `enabled` must stay false; this starts local input
only. The converter rejects unknown/duplicate config fields and any byte change
to its config during operation. The A2 host independently revalidates its frozen
config and emergency sentinel.

## Production path and trust boundary

`OpenXR locate -> immutable ALVR evidence -> packet decode/binding/replay check ->
TrackingManager accepted evidence -> local message pipe -> production source
codec -> inherited bounded JSONL control stream -> A2 input -> existing
Common World authority/owner/store/receipt -> Ordered Publisher -> wire 2.2`.

The raw local export is an internal versioned format, not a new authority or
HIL envelope. It carries the original seven IEEE754 binary32 bit patterns,
decimal integer strings, flags and exporter lifecycle. The converter uses the
fixed production `EncodeEnvelope`; the A2 host uses its production decoder and
all existing authority, sequence, callback, owner and durable receipt gates.
Receiver and Protocol implementation are unchanged. Source scope tests compare
all historical production algorithms and behavioral tests to the R2 Git archive.

The converter owns one message pipe whose DACL grants the current Windows user,
rejects remote clients, and fails on first-instance collision. The trusted
principal is that local user: another process acting as that same user is inside
this boundary. This is not process attestation or cryptographic device identity.
Protect the executable and external config using that installation's existing
permissions. Both child control channels are inherited local pipes, never an
unauthenticated network listener. The adapter never starts the tracking runtime.

Operator JSONL commands `prepare` and `source-authorize` explicitly request the
corresponding existing A2 operation. Until then, envelopes are discarded. Each
request first resets the converter connection and waits for its disconnect
barrier, so the authorized publication begins with a fresh sequence-0 bootstrap.
Every A2 command binds the actual runtime instance, control session, source
generation and source lease obtained from the host. The adapter cannot write
owner state, revisions, receipts or publisher state directly. Initial `prepare`
uses A2's existing owner/commit behavior only on explicit operator intent.
Optional `runtime-control` forwards existing gated operations; activating still
requires an explicit current readiness token and `explicit_enable`. Nothing in
this adapter obtains readiness or activates automatically.

## Time, pose and validity

Source time is signed OpenXR `XrTime` locate evaluation nanoseconds, a runtime
monotonic domain with an implementation-defined epoch and precision. It is not
sensor capture time or wall-clock time. `openxr-locate-<source-token>` identifies
that locate-clock lifetime. It stays stable across a network/pipe reconnect of
the same OpenXR source and changes with a new OpenXR source token. No clock
synchronization or subtraction between unrelated source, converter, A2 or
receiver clocks is claimed. Export diagnostics are elapsed `Instant` nanoseconds
since exporter creation. Envelope `timestamp_kind` is `receive`; timestamp and
sent-at are converter `steady_clock` nanoseconds. Its fresh `clock_id` identifies
the publication clock lifetime; the clock's epoch and actual resolution are
implementation-defined. JSON carries authority integers as decimal strings.

Original position is SI metres, right-handed OpenXR STAGE: +X right, +Y up, -Z
forward. Orientation is Hamilton quaternion `[x,y,z,w]`. The exporter copies
pre-transform VIEW-in-STAGE source bits, including signed zero. The converter
restores binary32 and exactly widens each value to binary64. It applies no
recenter, prediction, fusion, calibration, axis change or quaternion repair.
The unchanged A2 mapping applies later. Nonfinite or nonunit input cannot create
a usable pose. All six original validity facts are preserved. Missing valid or
tracked facts produce the existing `source_unavailable`; no replacement pose is
fabricated. Authority-lost/malformed binding closes the exporter channel.

## Ordering, lifecycle and fencing

The immutable observation ID and reference generation are monotonic nonnegative
integers within the existing signed-64 authority bound. Locate time cannot
decrease within a token. Exact duplicates emit nothing and consume no sequence.
Stale observations are rejected without refreshing trust. Conflicting facts at
one observation ID retire the token and close the stream. Source/reference
definition changes flush queued data and create a new publication bootstrap;
they never relabel an old observation into a new generation.

An exporter connection has a fresh OS-random 128-bit session. A converter
publication obtains fresh canonical UUID session and clock IDs. Sequence starts
at 0 for source authority, then 1,2,... for pose/unavailable. No-data produces no
envelope or sequence refresh. Late input must match the current exporter session
and A2 callback/lease; sequence alone is never a stale-session fence. Session and
source-token tombstones are bounded without eviction: 128 exporter/source
lifetimes and 256 publication lifetimes per converter process, after which it
fails closed. A converter process restart obtains a new runtime instance and
fresh sessions; A2 and the durable store retain their independent protection.

Snapshot distinguishes ActiveData, TemporaryNoData, Disconnected,
OrderlyShutdown, StreamFailure, InvalidExport and Backpressure. Explicit lifecycle
messages precede normal source close. An unexplained broken data pipe is a
disconnect; malformed/truncated/oversized data is a stream failure. EOF of the
control pipe is converter orderly shutdown only with complete framing. Unexpected
converter EOF is adapter failure. Already framed terminal messages are consumed
before EOF. A reference/session change obtains a fresh A2 source generation and
lease, revokes owner approval/publication and requires explicit source-authorize.
Reconnect neither clears durable Common World revisions nor grants eligibility.

## Bounded buffering and diagnostics

ALVR has no application queue. The unchanged A2 control host polls at 20 ms.
To leave processing headroom,
steady accepted source observations are exported at most once per 50 ms (20 Hz
cap; actual cadence follows available observations). The next eligible original
observation is selected; its pose, observation ID and source time are unchanged.
Skipped raw observations are counted as `export_sample_skip_count` before any
publication sequence is allocated. This thins only authority export, preserving
ordinary ALVR tracking/rendering. Reconnect, source/reference definition changes
and validity changes bypass the rate limit so lifecycle/lost evidence is immediate.
`export_min_sample_interval_ms` exposes the policy. The test-only saturation seam
uses zero interval; production has a fixed 50 ms interval. Bursts/status churn
can still reach the bounded overflow gate and close the stream safely.

The protected message pipe has 8192-byte buffers
and each export frame is at most 2048 bytes. Nonblocking writes must complete the
entire message; zero/short/failed writes close the connection and count drops.
The converter queue defaults to 16 envelopes (allowed 2..128); overflow closes
the connection and counts both the rejected item and flushed queued items.
Python queues hold at most 16 producer events, 16 operator commands and 8 A2
responses, with line limits 32768, 8192 and 131072 characters respectively.
Framing/queue overflow fails closed. Requests are serial and response IDs must
match. There is no silent reordering or unbounded deferred work. OS pipe
backpressure cannot grow the application queue; stale queued events are flushed
or rejected at the reconnect barrier. Shutdown closes both child input pipes,
waits for exit and terminates an unresponsive child after the bounded deadline.

Machine-readable snapshots include build commit, canonical ID, source type,
runtime instance, connected/lifecycle, generation/session/clock/token, reconnect
count, sequence high-water, last emitted envelope, stream failure, queue depth
and capacity, export/converter drop/reject counters, source locate and receive
clock metadata, validity and lost state. The adapter adds actual last successful
A2 handoff with lease, generation, session, sequence and local monotonic time.
Standalone converter handoff is null because only the adapter can attest it.
Snapshot contains no credentials or raw config/exception text.

## Software verification and artifacts

New C++ tests execute production source codec, A2 controls, actual file revision
store, exact receipt gates, Ordered Publisher and wire 2.2 encoder/decoder. Only
OS transport/entropy seams are deterministic there. Additional tests run actual
native local pipes, Rust accepted TrackingManager export, the converter and the
existing real OFF A2 host without activation. The unchanged real R2 receiver
decodes fresh A/B outputs on isolated software loopback and rejects B for an
explicit association-to-A, including otherwise identical wire fields.

`scripts/trusted_live_source_artifact.py` packages an immutable producer/adapter
bundle with source identities and file hashes. A candidate has an explicit dirty
source status; final packaging requires both phase trees clean and committed.
Running the packager never executes ALVR, opens a device, starts A2, binds a
socket or installs anything. Run `verify` to check its exact inventory/hashes.
All mutable configs, state and reports belong outside the artifact. The final
external phase report records executable, manifest, archive and template hashes,
fresh software counts and unchanged 5S. The next work is 5AH-E2 environment
re-binding, followed by 5AH-B; neither was performed in this phase.
