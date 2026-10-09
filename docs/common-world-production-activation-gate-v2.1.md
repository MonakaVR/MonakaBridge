# Phase 2B-5AG — Production Activation / Deployment Gate Foundation

Fixed base: `01c73b30bf7276ca765566ffb4b131b810341a43` (5AF).
Branch: `feature/common-world-production-activation-gate-v2.1`.

## Configuration and registration

The new `monaka_common_world_activation` composition and dedicated
`monaka_common_world_runtime` host opt in to the unchanged 5AF sender. Legacy Bridge
service, SteamVR driver/UI, user configuration, protocol kit, source receiver,
Owner/stamper, persistent runtime/store, ingress and sender are byte-exact.
Only CMake and exact new-file allowlist insertions in the prior scope checks change.
Those checks retain every original assertion; the new scope check proves this.

Configuration comes only from an explicitly supplied JSON file. No environment,
registry, user/machine settings, discovery, automatic endpoint or fallback exists.
An absent source, empty file, empty object, or explicit boolean false is OFF. A load
failure, malformed/duplicate-key JSON, non-object, oversized (>64 KiB) or deeply
nested (>8) configuration, unknown key or invalid enabled value fails closed.
Disabled configuration cannot open storage, construct a live owner or connect.
Enabled configuration requires all of:

| Field | Rule |
|---|---|
| `enabled` | Explicit JSON boolean true; readiness and explicit runtime activation are still required |
| `mode` | Explicit `production`; test registry accepts only `test` |
| `transport` | Production: `udp-ipv4`; test: `deterministic` with test-only registration |
| `codec` | Exactly `monaka-wire-2.1`, the actual pinned production codec |
| `endpoint` | Production: canonical literal `udp://IPv4:port`, port 1..65535; no DNS, wildcard, loopback, multicast, link-local or broadcast host; test: exactly `test://sink` |
| `owner_id`, `world_space_id`, `source_publisher_id` | Required nonblank UTF-8 Id, <=96 bytes, no control characters |
| `revision_path` | Required path; relative paths resolve against the explicit config file directory |
| `queue_capacity` | Optional, default 64, integer 2..4096 datagrams |
| `retry_limit` | Optional, default 3, integer 1..32, only 5AF manual known-zero retry |
| `peer_policy` | Exactly `explicit-authority-association-no-implicit-world-replacement` |

There is no automatic reconnect configuration. A rejected endpoint/codec/transport
combination is never repaired. Public production construction installs only the
literal IPv4 nonblocking datagram adapter. It cannot receive a fake factory.
Test dependency registration is defined only in the native test translation unit;
the production target contains no fake/loopback transport. Registration mode is
checked before opening persistence. Test mode with a production endpoint and
production mode with a test registration both fail closed.

The dedicated host accepts an explicitly selected typed source JSONL stream:
`monaka_common_world_runtime CONFIG AUTHORIZED_SOURCE_JSONL [--enable-production]`.
Config alone prepares and reports ReadyButInactive. Only the extra explicit enable
calls the gate, using the exact prepared durable world. The configured source
publisher and pinned codec validate the bootstrap. This file/typed boundary is a
trusted caller integration seam, not device discovery or network authentication.
The host is never invoked with production enable during this software phase.

## Startup, authority and activation

`prepare` runs config load -> validation/mode registration -> persistent recovery
-> fresh Runtime Owner epoch -> source approval and exact durable readiness commit
-> inert endpoint construction -> gate -> ReadyButInactive. Failure prevents every
later stage, destroys the candidate source/owner, and never attaches it to the runtime.
Recovery and commit run without the activation mutex. A nonwrapping activation
generation retires an in-flight startup on deactivate, owner revoke or shutdown.
After a delayed operation returns, the generation must match before attachment.

5AF constructs dormant Owner, source receiver and ordered stamper in its sender
constructor. This existing composition is preserved: at OwnerInitialization the
staged transport has no underlying endpoint. SourceReadiness authorizes the exact
source publisher/session/clock and commits current durable state. SenderConstruction
then registers the inert endpoint. No source/publisher eligibility or transport I/O
is inferred from construction. This is the precise adaptation of the requested
startup order to the existing architecture.

`activate(expectedWorld)` requires explicit exact current world identity. It checks
valid enabled config and matching registry; completed recovery; source/Owner approval;
no pending transition or persistence operation; live world and mapping; configured
installation/world-space identity; exact world owner/epoch/space/convention/revision;
CURRENT exact durable receipt and authorization epoch; endpoint/codec registration;
and no previous publisher queue, attempt or transport session.

The unchanged sender first connects to a staging adapter (readiness only, zero OS I/O),
then explicitly enables a fresh publisher session/clock and queues its two bootstrap
datagrams. Finally, the actual endpoint opens under the store's exact
`acceptCurrentCommit` gate and the activation mutex. A connect failure or exception
retires that publication/transport generation and leaves status Faulted, never Active.
The **activation linearization point** is setting Active after successful endpoint
activation inside this current-receipt critical section. External store handover
cannot interleave there; local revoke/deactivate/shutdown/source calls serialize on
the activation mutex. A concurrent revocation waiting for it is effective afterward.
There is no real-time lock latency guarantee.

Each `pump` still enters the unchanged 5AF final gate: CURRENT exact stored receipt,
current source/Owner/world/mapping, publisher/transport generations, production
serialization, cancellation recheck, then the known nonblocking submit API. The
**send linearization point** remains this endpoint submit entry. An epoch change
after activation is rejected at that send gate; Active is an observation of the last
transition, not future authorization. Status never grants rights. Snapshot fields
are returned copies and cannot mutate the runtime. The atomic read-only `status()`
also exposes Activating/Deactivating while the detailed snapshot mutex is busy.
Sender retirement observed on
receive/pump/complete immediately retires activation and closes the endpoint.

## Deactivation, shutdown and races

Deactivation holds the activation mutex, invalidates the activation generation and
calls `sender.disablePublisher()` first. That sender operation clears eligibility,
old queue and reserved/pending attempt under its lifecycle mutex: this is the
**deactivation linearization point**. It then disconnects the transport. No subsequent
pump/complete can enter through the activation boundary. Already entered datagrams
cannot be recalled. Late completion carries both a weak session reference and the
activation generation, in addition to 5AF's immutable attempt and sender generations.
It cannot pop a fresh head, advance a cursor, reconnect or activate.

Deactivation preserves the current source/owner and durable state for a later
explicit same-world activation, which uses fresh publisher and transport generations.
It performs no automatic reconnect or retry. Shutdown first performs the same
publication retirement/disconnect, then stops source, revokes Owner and disposes
the publisher/ingress composition. The persistent store remains unchanged and its
interprocess lock stays held until runtime destruction. Shutdown is terminal;
repeated activate/deactivate/shutdown calls are safe. Object destruction concurrent
with a caller executing a member requires normal C++ caller lifetime coordination.

Endpoint/world/source reconfiguration is not introduced. A second prepare on a
prepared runtime returns already-prepared. A source transition requiring a new world
retires publication and is never automatically persisted or activated. Restart
retains the installation/world-space identity and durable revision high-water,
requires a fresh authorization epoch and a durable successor, and cannot use the old
world/receipt/completion to activate the new runtime.

## Software boundaries

Delivery remains **ordered best-effort local submission**. The endpoint is nonblocking
IPv4 UDP and success means full local datagram API acceptance. Bootstrap spans two
datagrams, not a remote transaction. No application-level ACK, receiver commit,
exactly-once, duplicate-free delivery or cross-network receive ordering is claimed.
No durable publication cursor or automatic ambiguous retry is added. Actual pinned
EncodeEnvelope/DecodeEnvelope retains wire identity/revision mapping and permitted
finite binary64 exact-copy behavior; no float32, normalization or substitute codec.

The peer-policy string is a required explicit deployment policy selection, not proof
of peer authorization/reset. The unchanged receiver rejects a new world while it
retains an older world without the required ordered revocation. This phase refuses
implicit local replacement and does not claim seamless remote restart/handover.
Receiver association/reset, endpoint reachability, losses/reordering, physical
tracking, timing, driver integration and HIL remain independent deployment gates.
No network authentication, reviewed physical backend or remote trust is invented.

Persistent durability retains 5AD's Windows/local NTFS process-crash recovery scope.
Power-loss durability is unproved. Transport faults do not rewrite persistence,
Owner/source state, world identity or revision high-water; publication alone retires.
No real endpoint connection, SteamVR/SlimeVR output, deployment, HIL, pairing,
installer/service registration, driver, firewall or OS/global setting change is run.

## Verification matrix

| Fault/race | Expected software result |
|---|---|
| Missing/empty/false config, ordinary host launch | OFF, zero storage/endpoint/send side effects |
| Invalid JSON, endpoint/transport/codec/Id/world, queue/retry/peer policy | Fail before recovery and later stages |
| Every startup stage injection including load, validation, recovery, owner, source, endpoint construction, gate, enable, activation | No later stage or outbound submit; never partly Active |
| Recovery pending, absent owner, missing endpoint/publisher construction, corrupt state | Activate denied, zero connect/send |
| Epoch stale before gate or immediately before real endpoint entry | CURRENT receipt rejects; zero connect/send |
| Deactivate/revoke/shutdown while startup is paused | Generation retired; delayed candidate cannot attach |
| Concurrent activate vs deactivate/revoke/shutdown | Serialized linearization; after effective stop no queue, eligibility or new send |
| Revoke immediately before send or external handover at final send gate | Zero endpoint submit |
| Pending local completion after stop, restart or explicit reconnect | Stale rejection; no fresh head/cursor/activation mutation |
| World owner/epoch/space/convention/revision mismatch | Exact identity rejection before connect |
| Durable installation/world mismatch on restart | Recovery rejects before owner initialization |
| Source world transition | Retire, no implicit durable replacement |
| Production registration vs test config and test registration vs production endpoint | Rejected before storage/transport |
| Transport connect/submit fault | Publication retires; durable bytes/current owner/world preserved |
| Actual codec wire bytes and native NTFS successor recovery | Id/revision correspondence, ordering and allowed binary64 preserved |

## Executed validation (2026-10-10 JST)

All final commands exited 0. Visual Studio 18 2026 x64 Release, pinned OpenVR SDK
`0924064316de3effbcd1acf1e309182a2deb1c05` (build only), Python 3.13 and the existing
JDK 17/Gradle 8.14.4 offline wrapper were used. Sandbox MSBuild startup and Gradle
cache locking required execution outside the sandbox; no deployment action followed.

| Gate | Actual result |
|---|---|
| New native 5AG | **81 cases / 882 assertions PASS** |
| Bridge CTest | **36/36 PASS**, all 33 prior gates retained, 3 new gates |
| Live Source / generation | **41 cases / 696 assertions PASS**, original suite byte-exact |
| Persistent Revision Store | **32 cases / 513 assertions PASS**, original suite byte-exact |
| Sender / Transport | **41 cases / 838 assertions PASS**, original suite byte-exact |
| MonakaVR Core/Desktop | **1,875 / 153 PASS**, XML failures/errors/skipped all 0 |
| Original actual receiver replay | **14 streams / 82 events PASS**, unchanged receiver `299dda9d31d98ac28da018c5be2a916e36193566` |
| 5AF sender actual receiver replay | 2 positive streams / 10 events, 1 expected-rejection stream / 6 events PASS |
| 5AG activation actual bytes | Pinned oracle and actual receiver, 1 stream / 3 events PASS |
| 5S preservation | **4,285 SHA256**, 8,376 inventory entries unchanged; installed driver/root config/other repository states preserved |
| Static scope and whitespace | Prior checks retained, exact frozen files and `git diff --check` PASS |
| Real production endpoint/deployment/HIL | NOT RUN |

Final commit/remote HEAD and clean status are recorded in the accompanying local report under
`build/reports/phase2b5ag-production-activation-20261010/`.
