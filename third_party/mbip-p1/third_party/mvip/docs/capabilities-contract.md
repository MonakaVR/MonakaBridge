# Capabilities contract — Phase 2C-P1 (normative)

This specification follows the explicit 2C-P1 instruction and the exact P0
findings in `p1/p0-capabilities-findings.md`. It supersedes the historical Task 1
branch/base and C1-only implementation plan for this phase. It changes no wire
version, field, unit, coordinate convention, publisher identity, or session policy.

## Meaning and scope

`capabilities` MUST describe semantic channels that the device/source endpoint
can provide during its current device/session contract. `validity` describes
whether the current channel sample is usable under producer/admission policy.
Capability membership MUST NOT imply current validity, sample presence, tracking
quality, freshness, admission, body assignment, or a recovery/Fusion state.

A declaration is a set scoped to `(protocol family, publisher_id if present,
source_id, device_id or tracker_id, session_id)`. Pose and metadata messages for
this key MUST have the same set. Ordering is semantically insignificant.
Temporary loss, stale samples, absent optional fields and reacquisition MUST NOT
change the declaration. A semantic support change requires an explicitly admitted
new session contract; creating/authorizing that session remains the receiver's
responsibility. This library MUST NOT rotate identity, clocks, session or space.
New sessions MAY declare a different set. Reordering the same set is permitted.

## Vocabulary and representation

Only the seven frozen tokens are allowed: `position`, `orientation`,
`linear_velocity`, `angular_velocity`, `linear_acceleration`, `battery_fraction`,
`charging`. `schema/capabilities.json` is the vocabulary source. Generated native
and JVM enums MUST match it and the retained wire schema. Unknown tokens are
rejected as `UnsupportedValue`; duplicates as `InconsistentValidity`. Empty sets
are permitted for no-support/metadata messages when all other codec rules hold.
There are at most seven unique entries; existing 4096-byte/depth-16 bounds remain.

Legacy codec serialization MUST preserve list order and canonical bytes.
Semantic set comparison MUST ignore order. New explicit declarations use the
vocabulary order for deterministic materialization; this MUST NOT reorder packets
merely decoded/re-encoded by the legacy codec.

## Sample presence and availability

| Channel | Current usable-sample signal (after full wire validation) |
|---|---|
| position | pose `validity.position`; true requires finite non-null P and advertised support |
| orientation | pose `validity.orientation`; true requires valid non-null Q and advertised support |
| linear_velocity | non-null derivative/vector AND effective position validity |
| angular_velocity | non-null derivative/vector AND effective orientation validity |
| linear_acceleration | non-null gravity-free derivative/vector AND effective position validity |
| battery_fraction | battery exists and fraction is non-null (zero is a sample) |
| charging | battery exists and charging is non-null (false is a sample) |

The old wire has no independent derivative validity bit. This conservative
projection retains the frozen Bridge's parent-validity gates: velocity/linear
acceleration require usable P, angular velocity requires usable Q. A residual
non-null derivative during parent loss is diagnostic INVALID, not usable just
because a field exists. Independent derivative validity without its parent
requires a future model decision, not a guessed flag. Battery uses non-null
metadata independently. All usable samples remain subject to external freshness.
A producer SHOULD omit/null an unusable optional derivative; it MUST retain its
capability. P/Q with false validity MAY
retain finite diagnostic numbers, which MUST NOT be consumed as usable channels.
Unsupported diagnostic P/Q does not establish support. Non-null derivatives
without support remain codec errors. Metadata MUST NOT create/refresh a P/Q sample.

Projection distinguishes `UNSUPPORTED`, `ABSENT`, `INVALID`, `VALID`. Support is
independent of sample presence. `VALID` is effective sample validity before
external freshness/mapping/authorization checks; it MUST NOT bypass those checks.

## Same tracker and backend admission

One identity MAY retain position/orientation/angular_velocity support while its
current position is invalid and orientation valid. C2 expresses this as
`rotation_only`; PICO-like loss without proven orientation support uses `none`.
Neither creates a second device/tracker. Angular velocity is not proof of raw gyro
or an independent IMU. Separate asynchronous raw IMU streams remain out of scope.

`raw valid -> CANDIDATE -> ADMITTED` is backend-private. Candidate samples MUST
carry effective invalidity while retaining support, source/device/session/space.
Admission MUST NOT restamp old samples or change immutable content at the same
sequence. Tests use advancing samples with preserved acquisition/receipt age.
No shared CANDIDATE enum, timer, PICO heuristic, Altra ownership or Core recovery
policy is introduced.

`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`

## Legacy compatibility and migration

The selected frozen v2 codec/model/schema and fixtures are imported individually
from commit `0b4a27c2e2de88705c92fb6dbf5a8d9ca752bf39`, with content hashes.
There is no feature-branch merge. Tracking/Observation messages remain wire 2.0;
source authority remains 2.1; publisher-bound Common World remains 2.2. Common
World has no capabilities field and is not a capability-bearing tracker sample.
The codec MUST continue decoding those messages without fabricating capabilities
or source_publisher_id. Capability projection explicitly rejects non-tracking
messages; it does not reinterpret HMD authority as a tracker capability declaration.

P0's old dynamic Bridge is not a conforming static producer. An opt-in MBIP
adapter MAY normalize it only from an explicit provisioned structural declaration
for that exact source/tracker/session. It MUST NOT infer support from validity,
field presence, packet unions or vendor name. Default strict mode rejects
declaration changes. Adaptation preserves all fields except the declared
capability list and rejects any advertised channel outside the provisioned set.
Without the declaration, the old dynamic stream is REJECTED, not guessed.

This corrects semantics already expressible by the frozen encoding. No wire bump
is required. A new token, independent derivative validity field, raw-quality
subsystem, or identity model would require a separate architectural decision.
