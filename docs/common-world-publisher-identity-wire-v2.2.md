# Phase 2B-5AI-P Publisher identity propagation

Fixed Bridge base: `da62afb0c356bc0dae701922770a0697230f1cb7`.
The receiver-only 5AI-R STOP evidence demonstrated that source-publisher-A and
source-publisher-B, independently authorized with the same source_id and other
facts, produced identical Common World packets. Source authority publisher_id
was discarded by the owner's source_space copy. Persistent anchors / receipt
keys also omitted that publisher. The Common World header publisher_id names the
Bridge installation and cannot substitute for the upstream identity.

## Production identity path

The existing trusted source validator admits the exact configured authority
publisher_id. It remains wire 2.1 and no live source producer is implemented.
Publisher-aware runtime initialization binds an explicit validated sourcePublisher
to CommonWorldPersistentHighWater. The owner requires the admitted authority to
match that stable publisher and includes it in every immutable Common World
publication, pose, unavailable and revocation model as source_publisher_id.

Publisher-aware transition anchor equality includes its actual publisher. Schema-2 revision
candidates include sourcePublisher; transition/reestablishment anchors retain
publisher_id; mapping receipts retain sourcePublisher. Exact receipt comparison
includes all these values. The persistence store validates coherent publisher
identity before commit and checks current authorization / exact durable candidate
before publication. Foreign candidates/receipts cannot mint authorization.

The ordered publisher verifies coherent source identity across a bootstrap and
the entire publisher session before assigning an ephemeral Bridge header. The
sender copies that immutable envelope, exact receipt and runtime context into its
bounded queue. Final CURRENT store authorization and world/mapping/source identity
checks occur at the existing guarded send boundary. Identity is never read from
an endpoint or decorated from current config after persistence. Known-zero retries
reuse bytes; reconnect changes publisher session headers but retains publisher.
Revocation / stale generations retire work instead of substituting identity.

The supplied production C++ codec emits the explicit C2.2 namespace and required
field. The supplied pure JVM/C++ decoder exposes owner_id, world coordinate_space.id
and nullable source_publisher_id directly. No production receiver runtime is added.

## Version and compatibility

No 2.1 extension provides required upstream identity safely to an old decoder.
Wire 2.2 therefore introduces the six Common World types under the new namespace
`monaka.common_world_identity`, major 2 / minor exactly 2. It follows C2.1's
new-message precedent without adding a required field to an old message.
Every old 2.1 vector, schema, kit ZIP / pin and canonical byte encoding is retained.

| Encoder | Decoder | Outcome |
|---|---|---|
| 2.1 | original 2.1 | unchanged |
| identity 2.2 | 2.2 | canonical upstream publisher retained |
| identity 2.2 | original 2.1 | UnsupportedMessage, new namespace |
| legacy 2.1 | 2.2 | legacy model with null publisher |

An unknown legacy optional field named source_publisher_id remains ignored and
cannot provide trusted identity. A present model field requires version 2.2;
encoding with version 2.1 fails rather than silently relabeling it. Production
activation requires monaka-wire-2.2. Explicit test / legacy configurations may use
2.1 and schema 1; they make no publisher-aware association claim. OFF remains the
default. Config parsing / constructing an inert transport does not contact a peer.

## Persistent revision schema

This is sender revision persistence, not receiver association metadata.

Schema 2 uses magic MCWREV02, little-endian uint32 version 2, explicit high-water
owner/world/source publisher, retained revisions, authorization history and the
latest exact candidate. UTF-8 strings have bounded uint32 byte lengths. Candidate
publisher and authority publisher are persisted separately and validated coherent;
mapping receipts also bind the publisher. The whole record retains the 65,536-byte
bound and trailing SHA256. Invalid version, framing, identity, mismatch, checksum,
truncation, trailing bytes or missing formerly committed record fail closed.

MCWREV01 schema 1 retains its original normalized receipt comparison and remains explicitly legacy-only. Bound mode rejects such a
record; legacy mode rejects a schema-2 bound record. No automatic migration or
identity inference exists. A restart with the same publisher restores high-water
identity but no active source/world authorization. A fresh owner epoch and source
authorization must create a strictly newer revision. Foreign configured publisher
is rejected before reserving an authorization epoch. Startup never clears a record.

The unchanged 5AD Windows fixed-NTFS backend keeps lifetime exclusive ownership,
same-directory temporary write, FlushFileBuffers and atomic replacement / existing
fault barriers. No sudden-power-loss durability experiment has been performed and
no additional power-loss, receiver durable-commit or application-ACK guarantee is
claimed. Delivery remains **ordered best-effort local submission**.

## Bounds and regression scope

Canonical publisher Id is case-sensitive, opaque, 1..96 UTF-8 bytes, nonblank,
control-free, unnormalized. Production JSON framing / strict UTF-8 / duplicate-key
and depth validation are unchanged. Whole packets remain bounded to 4096 bytes;
oversize encoding fails before local submission. Worst escaped maximum-width pose
vectors encode to 4023 bytes in C++ and 4017 in JVM. Numeric source/common payloads
retain binary64 exact-copy semantics and owner/world meaning remains unchanged.

The original 2.1 kit stays under third_party/monaka-protocol-v2. Production linking
uses an actually generated, hash-pinned clean-commit 2.2 kit under
third_party/monaka-protocol-v22. Both archives/trees receive exact integrity checks.
Historical scope gates have an explicit audited 5AI-P exception solely for the
identity/version path. Their prior assertions are retained and normalized only for
that extension; all historical behavioral tests remain byte-exact except production
codec fixture selection and the corresponding publisher-bound stored fixture in
activation/readiness tests. No assertions or cases are
removed. The frozen 5S baseline receives no exception or rewrite.

The new identity tests use deterministic source fixtures, test transports and
temporary revision records. They create no network sockets, production endpoints,
live producer, device operation or receiver runtime. Legacy regression loopback
fixtures remain separate from production. Final results / artifact hashes are in
the external phase report and are not inferred from historical PASS reports.

## Software gates

The implementation passed Bridge CTest 46/46 (all original 45 plus publisher
identity), new receiver-visible wire tests 29 cases / 14 cross-language directions,
new runtime identity tests 28 cases / 348 assertions, A2 60 / 763, readiness 55 / 713,
5AG 81 / 882, 5AE 41 / 696, 5AD 32 / 513, 5AF 41 / 838, host 13 and artifact integrity
10 / 10. Fresh Core 1875 / Desktop 153 and baseline replay 14 streams / 82 events
passed; additional sender replay 2 / 10 and negative owner-handover 1 / 6 passed.
The original 5S hashes 4285 / inventory 8376, E1 270 files and 5AI-R STOP evidence
remain unchanged. Final artifact / HEAD / push verification are recorded externally.

## Remaining phases

Production receiver listener / association persistence remain pending 5AI-R.
Trusted live ALVR/PICO/VIVE producer remains pending 5AI-S. 5AH-B remains stopped.
This phase performs no production activation, HIL, real peer contact, device,
firewall, registry, service, driver or global OS change, and no dev/main merge.
