# Phase 2B-5AC: Common World runtime owner and dormant ordered publisher

Base: `feature/trusted-hmd-source-receiver-v2.1@be97d2279ffd51ba6e66bd9bc19526164309691d`.
Acceptance consumer: unchanged MonakaVR `299dda9d31d98ac28da018c5be2a916e36193566`.
Protocol source: `ea1e02c15c20693070ce69cfbc54941ee7ae8e7d`; use the existing exact
wire 2.1 kit/manifest/lock. No schema, codec or protocol artifact is changed.

## Scope and serialization

`CommonWorldAuthorityOwner` owns the installation's Common World and M2 HMD binding.
`CommonWorldPublisher` is a distinct state machine for installation publisher ID, caller-supplied
canonical fresh session/clock UUIDs, local event time, contiguous sequence and bounded session history.
`TrustedHmdCommonWorldCoordinator` composes the existing source receiver and these two states.
All operations are synchronous and caller-serialized, including injected token factories; factories
must not reenter mutations. Accessors are const; generated events and snapshots are values.
They contain structural dormant evidence, never a Reviewed backend or production HMD capability.

Every source operation reconciles `streamValid()` **and** `currentAuthority()` after the receiver
operation. Return strings only select new-observation/unavailable propagation and source-proven reasons;
they never establish authority. Invalid stream, absent authority or changed exact anchor immediately
removes world/mapping and returns revocation, even when no pose follows. Pending requests are retired.

`beginSourceSession` is one atomic authorized handoff: select the fresh source publisher session and
apply its seq0 authority snapshot, then reconcile before returning. No independently observable
half-bootstrap is exposed. A healthy handoff with identical source ID/token/space/generation/kind/time
domain preserves the world, revisions and calibration. Failed bootstrap revokes immediately.
Gap/conflict/malformed/disconnect observed earlier has already killed the old epoch; even identical
later bootstrap requires a fresh persistent transition. The source receiver's existing standalone
`beginAuthorizedPublisherSession` API remains unchanged; callers composing it themselves must reconcile
after every externally visible transition and cannot suppress a real coverage loss.

## Persistent request and exact receipt

Construction requires explicit `CommonWorldPersistentHighWater`: owner installation Id, Common World
space Id, optional already-persisted world revision and already-persisted mapping revision. These
values are neither source generations nor publisher counters nor legacy Config fields.

New authority, source generation or recovered coverage creates `CommonWorldTransitionRequest` with
owner-lifetime nonwrapping request identity, both IDs, exact source-authority payload, minimum next
world/mapping revisions and reason. First world revision can be zero if no previous revision exists;
otherwise it must strictly increase. Mapping must strictly increase from the persistent high-water.
No epoch factory is called and no world/mapping/pose is produced while this request is pending.

`commitWorldTransition` requires `persistentCommitConfirmed=true`, equality of every request fact and
committed revisions at least the requested minima. Source publisher headers are not source authority
facts; exact anchor means ID/token/space/generation/kind/locate domain. Wrong, failed, stale, retired or
late receipts return no event and leave the world unavailable. Changing the pending anchor generates a
different request identity. Request identities never wrap, so A/B/A anchor changes cannot revive an
old request within this owner lifetime. Retiring an uncommitted request does not invent a durable commit.
An external store must reconcile any of its already-committed high-waters and issue a current receipt
at or above the minima. Once accepted, durable high-waters advance even if later token mint fails.

The receipt is an explicit **trusted caller attestation**, not cryptographic proof of disk I/O.
Actual persistent revision store/adapter: **NOT IMPLEMENTED**. The tests use deterministic synthetic
receipts. Existing `saveConfig` is not that store and does not satisfy its transaction/receipt contract.
No automatic Config mutation, save, file I/O or persistence adapter is introduced. Across-process replay
protection and store identity/recovery/atomic commit belong to the next phase; an owner must be seeded
from an actual persistent high-water before any future production integration.

## Epochs, world and mapping

Only after a valid receipt, injected independent factories mint a fresh world epoch and calibration
epoch. Both must pass the pinned codec's opaque Id rules. World and calibration token sets include live
and retired tokens, are independently bounded (default 128), never evict, and reject reuse across either
kind. Blank/invalid/duplicate output, factory exceptions or saturation close the owner permanently.
Tokens are never derived from time, PID, transport, source authority, SteamVR Universe or revisions.

World value: supplied owner ID, fresh opaque world epoch and explicit Common World ID with
`rh_y_up_neg_z_forward` and committed world revision. Its anchor is the exact current OpenXR source.
M2 mapping: that anchor, separately minted calibration epoch, committed mapping revision, positive
identity Hamilton xyzw `[0,0,0,1]` and translation `[0,0,0]` (`T_common_from_source`).

An explicit persisted `CommonWorldMappingReceipt` updates only the higher mapping revision in an active
world. World/anchor/calibration/identity transform remain exact. A lower revision is rejected; an equal
exact mapping is idempotent; equal changed source/calibration/transform bits revoke the mapping with
`mapping_conflict`. Wrong-world and unconfirmed receipts are rejected. Higher revisions cannot silently
change M2 transform or calibration. Only mapping publication is returned; no pose is replayed.

Mapping withdrawal removes the mapping immediately but leaves the world active. Re-establishment
requires a confirmed receipt naming the exact active world and anchor, higher persistent mapping revision,
and a newly minted calibration epoch. It emits mapping only. World withdrawal removes world/mapping
immediately. A later explicit replacement under the unchanged source uses `explicit_world_replacement`,
not a fabricated source-generation transition. UINT32 revisions and the request counter never wrap;
exhausted high-waters block a new world. Saturation closes the owner, not a bounded-history eviction.

## Poses, unavailable and immutable values

Only a newly accepted source observation is forwarded. One value snapshot freezes world, mapping,
source observation and evidence before construction. Observation identity, signed locate time, domain,
binary64 source P/Q and all six validity flags are copied. Common P/Q are byte-exact copies for M2:
no quaternion normalization, sign canonicalization, Float conversion or identity arithmetic. Source-token
observation high-waters survive owner transitions; duplicate/older observations cannot recover unavailable.
The coordinator also requires the source receiver's `pose` acceptance result, never its old candidate.
Poses accepted while persistence is pending are not retrospectively emitted on commit.

Source unavailable preserves world/mapping and returns CommonUnavailable with exact reason and nullable
validity. Recovery requires a genuinely new source observation. Mapping/world revoke does not wait for
another pose. Control events never refresh observation age. Publisher header time is the caller's Bridge
local monotonic event time, with `timestamp_kind=receive` and `timestamp_ns=sent_at_ns`; OpenXR locate time
is kept in source evidence and never substituted or subtracted from another process clock.

MonakaVR's dormant receiver independently applies its stricter Float-domain finite/unit-norm and raw-bit
mapping proof. Identity preserves its source/common Float projection. Generated streams are actually
replayed against the unchanged receiver. This does not widen the consumer's projection limits: a binary64
wire-valid quaternion outside its stricter Float norm gate remains consumer-unusable; Bridge does not
repair source evidence to pass that gate.

## Reason map

| Source-proven wire reason | Common world reason |
|---|---|
| source_session_loss | source_session_changed |
| event_stream_lost | event_stream_lost |
| source_space_ambiguous | source_space_changed |
| authority_withdrawn | anchor_authority_lost |
| capacity_exhausted | capacity_exhausted |

Local gap/conflict/clock/time-domain/malformed/disconnect uses conservative `event_stream_lost`.
Observation conflict uses `anchor_authority_lost`. Exact source-token replacement uses
`source_session_changed`; changed generation/anchor facts use `source_space_changed`.
`onSourceTransportDisconnected` invalidates the receiver and immediately returns world revocation if a
common session was bootstrapped. No source wire revocation is fabricated.

## Publisher lifecycle and event order

Publisher ID equals the owner's installation ID. Fresh canonical caller UUIDs for both session and clock
are validated by the pinned codec, retained forever within configured positive capacity, and never reused.
Capacity saturation closes only the publisher. Owner state survives publisher failure/restart/exhaustion.

Starting with active world **and mapping** returns seq0 WorldAuthority and seq1 Mapping full snapshot,
with the same owner epoch/revisions/calibration. No pose is replayed. A session without a complete active
pair stays silent; first activation emits that pair at seq0/1. A world with a withdrawn mapping also stays
silent until re-establishment can supply a coherent pair; a world-only seq0 followed by revocation would
violate the receiver's mandatory seq1 mapping. In a previously bootstrapped session, mapping/world
revocation always emits immediately. Bootstrap owner/world/source references are checked atomically.

All six common types use one contiguous sequence. World replacement: old world revoke, receipt wait,
new world authority, new mapping, then only new source observations. Mapping update: pose M10, mapping
M11, next observation pose M11. Withdrawal at EOF is valid and independently effective.
The publisher stamps and returns `vector<Envelope>` in order, prevalidates the entire batch through the
codec and reserves sequence space before returning anything. At INT64_MAX there is no successor; a
fresh session and snapshot are required. A reduced sequence ceiling is a deterministic test seam for
the identical terminal path. Negative/backward event time or invalid output closes the session.

There is no internal output queue or delivery retry. The caller must deliver the complete ordered vector
on an authorized future transport or mark transport coverage lost; dropping a control vector is unsafe.
The existing latest-only `Fanout::put` is never used. No new socket, thread, runtime service wiring,
automatic output, persistence or SteamVR/OpenVR authority readback exists.

## Legacy isolation and evidence

Existing Binding.world/worldSpace/worldRevision, Config.revision/JSON, profile normalization,
worldCalibrationCandidate, trajectory AX=XB/Standing calibrator, MTP v2.0 output, Direct, routing and GUI
are unchanged. SteamVR is not Common World authority. No PICO/VIVE mapping or MonakaVR source is edited.

`common_world_runtime_tests` exercises owner, receipt, tokens, exact numerics, unavailable, source failure,
mapping/world withdrawal, publisher restart and exhaustion, and generates JSONL under
`build/common-world-publisher-fixtures`. Every artifact event passes EncodeEnvelope/DecodeEnvelope.
`validate_common_world_streams.py` reuses the unmodified pinned C2.1 test-only ordered oracle without
creating bytecode in the kit. `CommonWorldGeneratedReplay.java` is an external read-only MonakaVR
compatibility harness; it checks actual receiver results and immediate withdrawal at EOF.
Full Bridge CTest, historical v2.0 pin, fixed 2.1 pin, and unchanged MonakaVR focused tests are required.
Run details, hashes, preservation receipts and final results live in the Phase 2B-5AC report directory.

## Production gates and next phase

| Component | Status |
|---|---|
| Wire 2.1 | READY / PINNED |
| Source receiver / Common World owner / common publisher | READY / DORMANT |
| Persistent Common World revision store | NOT IMPLEMENTED |
| ALVR wire 2.1 sender / live source transport / live common transport | NOT IMPLEMENTED |
| MonakaVR common receiver | READY / DORMANT |
| Reviewed backend | NOT ESTABLISHED |
| Strong Trusted | UNSUPPORTED |
| RawHmdPoseInput | BLOCKED |
| Position Correction | NOT ENABLED |
| 2B-5P | NOT READY |

No driver/config installation, 5S deployment or HIL is performed. Next phase:
**Phase 2B-5AD — Common World Persistent Revision Store / Runtime Owner Integration**.
The missing durable transaction/receipt adapter is the remaining central Bridge blocker.
