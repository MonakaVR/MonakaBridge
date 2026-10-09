# Phase 2B-5AE: Common World source callback / runtime ingestion

Frozen base: `f95b8c012ea0c47a38bea1c4a485bbf21556668a` (5AD). The source receiver,
Runtime Owner, Ordered Publisher, coordinator, persistent runtime, store schema/backend,
protocol kit and all prior tests are byte-exact. The new `CommonWorldLiveIngress` composes
the existing `PersistentCommonWorldRuntime` through its established public APIs.

## Audited topology and explicit activation

The actual Common World source abstraction is `TrustedHmdSourceStream`: a synchronous,
caller-serialized in-process typed receiver (also with a fixed-kit bytes decoder). It has
no listener/thread. `TrustedHmdCommonWorldCoordinator` feeds its accepted snapshots to
the owner; `PersistentCommonWorldRuntime` wraps durable receipt and authorization gates.
These are absent from `tools/bridge_service.cpp`. The separate production Observation UDP
receiver/router in that service handles ordinary TrackerObservation and its existing
registry/mapping/calibration/fanout; it is not an authority source. This phase connects
the existing typed Common World receiver boundary and introduces no network topology.
ALVR production source transport/identity provisioning remains a later integration boundary.

The adapter has no automatic start, registration, worker, queue or production composition.
Creation explicitly reserves a fresh runtime authorization epoch through the unchanged
store contract. `start(at)` separately creates an opaque callback handle and grants no
owner approval. The trusted controller must explicitly call `authorize(handle, publisher,
freshWireSession, clock, bootstrap, at)`. That only creates the existing pending request.
`commitPending(handle, at, cancellation)` runs the durable revision pipeline; only the
exact receipt and current gates can create a world/mapping. A pose callback before approval
returns `owner-not-approved`; an approved callback before durable acceptance cannot publish.

```text
controller start -> fresh local generation, weak callback context, owner approval false
controller authorize -> current handle check -> 5AD current epoch gate -> 5AB bootstrap
 -> exact source/identity facts -> 5AC pending transition (no world/mapping rights)
controller commitPending(handle by VALUE)
 -> current source/approval + single persistence slot
 -> 5AD candidate -> durable commit outside lifecycle lock -> exact receipt
 -> same captured source generation + current approval
 -> 5AD same candidate/actual current persisted bytes/owner identity/authorization generation/epoch
 -> CURRENT pending transition/anchor -> dormant eligibility observation
source Callback.receive(typed event, at)
 -> captured weak context/generation -> current source + approval + not busy
 -> unchanged receiver sequence/identity/validation -> unchanged runtime current epoch gate
 -> current committed owner/mapping -> exact common pose -> silent Ordered Publisher
```

Only authority/mapping transitions allocate persistent revisions. Poses are not a new
revision stream and are not persisted/replayed at commit. Source timestamps, sequence,
status and pose semantics remain those of the fixed normalized contract. No transform
or second normalization is performed. Publisher session is inactive by default. Its
explicit `beginDryRunPublisherSession` test seam returns ordered in-process vectors and
creates no sending capability. `eligible` and snapshots are observations at their gate,
never reusable runtime or outbound permissions. The live lifecycle API discards cleanup
dry-run vectors; it promises current eligibility invalidation, not a deliverable output
stream across stop/dispose. A future sender must define its own publication lifecycle.

## Independent lifetimes and linearization

| Lifetime | Identity and gate | End/restart |
|---|---|---|
| Source connection | Opaque callback's weak State identity + private U64 local generation | Every start/reconnect increments; stop/fault/dispose clears active before returning |
| Runtime authorization | Explicit controller approval, 5AD ticket authorizationGeneration, durable fresh authorization epoch, current Owner/anchor | Revoke clears approval while source can remain active; new approval requires new local generation and fresh wire session; external epoch handover uses current store gate |
| Revision identity | Exact tagged candidate, persistent world/mapping high-water and payload | Durable commit remains provenance even if source/owner is retired |
| Publisher | Existing separate session/clock/sequence, inactive by default | Explicit synthetic dry-run only; no transport/I/O |

The local generation is not wire `source_space_generation`, wire publisher session, tracker
identity, owner epoch or revision number. Handles cannot be forged through the public API;
equal numeric generations from different adapters are foreign. A new adapter after process
restart may begin local counting again, but weak State identity differs and the store
requires a fresh authorization epoch. Restore recreates no source session or owner rights.

The ingress mutex linearizes lifecycle, authorization, source admission and receipt handling.
Each callback resolves its weak state before use, so destruction cannot leave a raw pointer,
released handle or cancellation source in a callback. Cancellation is a caller-owned atomic
value used only during the synchronous commit call. Stop/dispose are idempotent; an old
handle's stop/fault cannot close a newer generation. Revoke keeps the source active and
permanently retires that generation's approval, including when no world existed yet.

Neither candidate preparation's store open nor persist holds the ingress mutex. Stop,
revoke and dispose can finish while storage is paused. A commit admitted before stop may
advance durable high-waters; its captured callback is passed by value, and its result
cannot be eligible after stop/reconnect/revoke/dispose. Current 5AD receipt validation
remains mandatory and store handover shares the same gate mutex. No store semantics change.
The only calls under the ingress lock are known synchronous runtime operations, whose
store gates use try-lock; no unknown source/listener callbacks execute under it. No
external callbacks or token factories are supplied by this adapter. Calling a lifecycle
operation after a callback returns is safe; there is no lock held across user code.
Synchronous OS file reads at a current gate retain 5AD's possible latency qualification.

## Ordering and backpressure

One synchronous persistence operation across all generations is admitted at a time. Its
one exact retry ticket is the only retained work item. Other commit calls, source receives
and approval calls during that operation return explicit `backpressure` before admission.
The caller retains and retries the identical event in source order after completion;
the adapter never claims success, queues, coalesces or silently drops those events.
Stopping/revoking does not cancel already committed storage. Queued caller work retains
the old handle and is rejected when resumed; no adapter-owned queued work exists.

Concurrent source calls serialize at the ingress mutex in arrival order. The protocol's
existing contiguous sequence is the source order: exact duplicate ignores, older sequence
ignores, immutable same-sequence conflict and sequence gaps fail closed. There is no
reorder buffer and no invented sequence. Concurrent differently sequenced callers must
preserve protocol order; reversal can close coverage and requires a fresh session.
Observation IDs/high-waters, token retirement and bounded non-evicting source/session
retention (default 128) are unchanged. Revision assignment is exclusively serialized
through the one persistence slot and unchanged durable store. No nondeterministic
parallel revision allocation is introduced. Invalid/gap/conflict source admission closes
the adapter generation; wrong protocol/unauthorized input cannot grant rights.

On write/flush/rename uncertainty or cancellation before publish, the adapter returns
`persistence-failed`, an error string and no receipt/eligibility. It retains the exact
ticket for the unchanged idempotent retry, including post-rename uncertainty. Successful
retry after such uncertainty does not allocate another revision. After successful atomic
publish, 5AD's cancellation contract remains unchanged: cancellation cannot undo a durable
commit. A source/owner fence still prevents its completion from granting stale rights.

## Exact data and verification scope

Typed envelope values go directly into the existing fixed receiver. Its EncodeEnvelope
validation is unchanged; there is no added deserialize/serialize round-trip, float32,
locale parsing, identity rewriting or arithmetic. Source/space/token/locate-domain identity,
observation IDs, locate timestamps and all validity flags copy according to 5AC. Source
wire header times are not turned into common header times: common headers retain the
existing receive-time rule; the source locate time/domain stay exact. M2 mapping remains
the exact positive identity. Positions/orientations retain all binary64 bits including
negative zero, subnormal values and quaternion sign. Nonfinite values, including NaN
payloads/signs and infinities, remain rejected by the existing wire contract; no new
nonfinite-copy guarantee is claimed. The persisted mapping/transform binary schema is unchanged.

Tests drive actual fixed-kit decoded envelopes and the actual Windows/local NTFS store.
Manual promises and storage boundary barriers force both callback/stop orderings and
BeforeWrite/BeforePublish/AfterPublish stop/revoke/dispose/reconnect orderings. Timeouts
are deadlock watchdogs only, never sleep-based success criteria. Additional simultaneous
duplicate and stop/dispose tests assert order-independent outcomes. 5AD continues to
cover exact/forged receipts, duplicate/reordered completions, corrupt recovery and real
child-process crash boundaries. New generated adapter vectors additionally replay through
the unchanged pinned oracle and real MonakaVR receiver. The execution report records
actual counts, logs, SHA256 inventory and clean normal-push evidence.

Outbound transport, live sender, SteamVR/SlimeVR output, default config, deployment,
device pairing, drivers and HIL remain inactive/unmodified. Real source transport timing,
hardware reconnect behavior and production identity provisioning are not validated here.
Durability retains 5AD's tested local NTFS process-crash qualification; physical power-loss
durability is not established. No subsequent phase or production activation is authorized.
