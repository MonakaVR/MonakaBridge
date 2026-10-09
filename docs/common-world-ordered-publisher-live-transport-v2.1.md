# Phase 2B-5AF: bounded Ordered Publisher / local datagram sender

Frozen base `55f6259a20a6afbb22889e20e513d83f0307ccf8` (5AE). Source receiver,
Owner, ordered stamper, coordinator, persistent runtime/store, 5AE ingress, protocol kit,
all prior behavioral suites and replay harness remain byte-exact. The prior 5AE scope gate
only extends its filename allowlist for this phase's seven new files; its original checks
remain exact, and a new 5AF gate verifies that extension plus frozen-base equality.

## Audited transport and delivery semantics (decided before implementation)

The actual existing `mb::Udp` binds/sends only IPv4 localhost. It is nonblocking and calls
`sendto`; success is a return equal to the entire datagram size. Maximum payload 4096 bytes.
It has a bool send API, no completion callback, error classification, peer confirmation,
application ACK or durable publication checkpoint. Existing Fanout is latest-only MTP routing,
not legal Common World revision queueing. HealthWriter retry belongs to diagnostic files,
not publication. `CommonWorldPublisher` is a synchronous ordered envelope stamper, advances
sequence on stamping, and promises no end-to-end delivery. No Common World sender existed.

The honest contract is **ordered best-effort local datagram submission**. A full local API
return is publication submission success, not peer/application delivery. Datagram boundaries
are atomic at this API, but world/mapping bootstrap spans two datagrams: it is not a network
transaction. UDP can lose, duplicate or reorder packets after local acceptance. The existing
consumer fails closed on sequence/identity/session problems; there is no ACK/reliability upgrade.
Exactly-once, at-least-once peer delivery and duplicate-free delivery are not claimed.

The unchanged MonakaVR receiver also requires an ordered old-world revocation before
replacement of an already established world. A fresh publisher session alone does not
authorize implicit world replacement. This controller retires old queued work on source/Owner
loss and cannot send that old authority's revocation after revoke under the final CURRENT
gate. Therefore a new durable world bootstrap is locally submittable, but a receiver still
holding the old world rejects it; no seamless peer owner handover or remote revoke notification
is promised. The real receiver replay tests that rejection and absence of a new pose candidate.
Same-world sender reconnect separately proves an accepted fresh snapshot. A future production
activation needs an explicit peer authority/reset/revocation policy; it cannot infer one from
socket connectivity or local acceptance. No receiver trust rules are weakened in this phase.

No persistent publication cursor is added. `lastLocalAcceptedSequence` is a volatile diagnostic
cursor that advances only after full local acceptance. Retirement preserves the last observed
value as historical diagnostics while disabled; a fresh explicit enable resets it. It grants
no eligibility or restart/retry permission.
The original stamp sequence is distinct from this success cursor. A crash after entry/local
acceptance can leave an ambiguous last delivery or lose queued poses. Reconnect explicitly
rebootstraps current durable state in a fresh publisher session and may repeat world/mapping
information. Restart loads durable revision provenance only: no queue, connection, cursor,
source/Owner authorization or publisher session is restored. Fresh authorization and durable
successor revision precede an explicitly enabled fresh session. No older revision is replayed
from an implicit checkpoint. Network receive order remains outside the local ordering guarantee.

## Composition and causal path

`monaka_common_world_sender` is a separate library and is NOT linked into the production
service, driver or UI. `CommonWorldLiveSender` exclusively owns an unchanged 5AE ingress,
the source/Owner mutation operations, bounded queue and sender endpoint. Its source handles
wrap the original opaque weak callback; no raw ingress/runtime mutator escapes this API.

```text
source start/authorize/commit -> unchanged 5AE generation fence and 5AD exact durable receipt
explicit transport connect -> separate transport generation, publisher remains disabled
explicit enablePublisher(exact receipt, fresh session/clock)
 -> actual current stored receipt + current source/Owner/identity
 -> existing Ordered Publisher snapshot -> typed world/mapping envelopes -> bounded FIFO
source poses -> actual source/Owner gates -> existing ordered stamper -> same bounded FIFO
prepare -> immutable reserved head (no send/pop/codec)
submit -> CURRENT exact committed receipt under store gate
 -> CURRENT source/Owner/world/calibration/mapping identity
 -> CURRENT publisher/transport generation + reserved attempt identity
 -> actual pinned production EncodeEnvelope -> cancellation recheck
 -> enter known nonblocking local datagram API (send linearization)
 -> full local acceptance OR pending/known-zero rejection/ambiguous failure
 -> corresponding volatile cursor/queue result (never ACK/checkpoint)
```

`connect` and source start grant no publication authority. `enablePublisher` requires current
durable receipt AND current owner/mapping, not a caller's cached `eligible` boolean. Earlier
eligibility is rechecked at every send. Receipts carry durable authorization identity separately
from local source/publisher/transport generations. The stored candidate and snapshot identities
are checked, not just revisions. Default construction performs no network I/O; only explicit
test-only connect opens the existing loopback UDP socket. Factory has no IP argument and rejects
port 0 and known production 29810..29813 ports. Caller must supply its own test sink/ephemeral port.

## Gates, lifetimes and send linearization

| Lifetime | Identity | Retirement |
|---|---|---|
| Source | Existing weak callback State + in-process generation, independent wire source/session facts | stop/reconnect/fault/dispose through 5AE |
| Owner | Existing runtime authorizationGeneration/epoch, world/calibration identity and approval | revoke/current-store handover; no send bypass |
| Durable revision | Existing tagged exact candidate and actual persistence receipt | current latest/epoch gate; unchanged Store semantics |
| Publisher | Existing fresh wire session/clock and stamp sequence + new local nonwrapping publisher generation | disable, source/Owner context loss, overflow, reconnect, failure |
| Transport | Independent local nonwrapping transport generation/connection | disconnect/reconnect/dispose/ambiguous result |
| Attempt | Private immutable Plan pointer, scoped generations + context/receipt/head | completion, failure, cancellation or any retirement |

Sender mutex excludes every source/Owner/publisher/transport mutation in this composition.
The store's existing `acceptCurrentCommit` mutex excludes external authorization handover.
Final snapshot validation, known bounded codec and nonblocking endpoint entry occur within
that same critical section. No unlocked gap permits an effective revoke after a final check
but before API entry. The **send linearization point is entry into the endpoint's nonblocking
local datagram API**, after which bytes may escape and cannot be recalled. A revoke request
concurrent with that short section becomes effective when it acquires the sender mutex; if
send entered first, that attempt is honestly on the post-linearization side. Returning
local success is a later completion boundary, not the authorization linearization point.

No unknown blocking writes/callbacks are permitted under these locks. The endpoint contract
requires submit/connect/close to be bounded/nonblocking, no controller/store reentry, and no
inline completion invocation. Pending may defer only completion of an already-entered attempt,
never initiate a future send without a new gate. The concrete endpoint is the audited existing
nonblocking UDP implementation; deterministic fake returns immediately and drives completion
manually. A future blocking stream implementation cannot use this interface without a new audit.
Disk prepare/commit remains outside sender/ingress locks. Current-state file reads retain 5AD's
possible synchronous OS latency; no real-time revocation latency is promised.

## Queue, retry, failure and reconnect

Queue counts datagrams, default capacity 64, minimum 2 for bootstrap; configurable finite
bound. Entire output batches are admitted or rejected. One reserved/sending head exists.
Later frames cannot overtake a reserved/pending/failed head. Stamping and enqueue serialize
with source callbacks; no new source sequence is invented and the receiver's contiguous
source sequence policy remains unchanged. Concurrent duplicate callbacks admit one pose.
Differently sequenced concurrent producers must obey the existing source ordering contract.

Overflow is explicit `queue-overflow`: close the whole publisher generation and clear its
queued work, with no silent success/coalescing. The source event may already have been accepted;
caller must observe its separate publication result. There are no blocked producer threads,
workers, timers, unbounded channels or background retries. Stop/revoke/dispose release pending
plans/queued work immediately; late completion cannot pop a newer head or regain authority.

Known-zero `Rejected` keeps the head for manual bounded retry (default 3, max configured 32).
No timer/busy loop; each retry requires an explicit pump. Exhaustion retires the generation.
The real bool UDP failure lacks a detailed acceptance/error boundary, so it is conservatively
`Ambiguous`, not automatically retried. Exceptions after API entry, timeout, disconnect,
cancellation after entry and any partial datagram acceptance retire publisher/transport;
cursor does not advance, no stale retransmit or later-frame overtaking occurs. Before-entry
failure/cancellation produces no API call. Partial stream writes are NOT APPLICABLE: this is a
datagram interface, not a fake stream. Partial datagram return is a contract violation, tested
as ambiguity. Malformed/split/corrupt JSON is tested by the actual decoder. No checksum exists,
so valid-looking numeric corruption cannot be cryptographically detected by this wire protocol.

Pending local completion is not success. Complete validates the exact immutable attempt,
current epochs/receipt/context/generations again. Duplicate/late completion is rejected after
stop/revoke/re-enable/reconnect/fresh runtime. These notifications are NOT application ACKs.
ACK loss/duplicates are protocol-not-applicable; inventing an ACK field would change the kit.
Bootstrap's first datagram may escape while its second fails: the second stays ahead of all
poses, or the whole session retires. A new authority revision also retires obsolete queued
work (the existing current-latest receipt contract forbids sending obsolete revisions).
New explicit durable/current bootstrap uses a fresh generation/session, not overtaking within
the failed old session. No source/transport reconnection self-authorizes that bootstrap.

## Wire integrity and software proof

Actual `monaka::protocol::v2::EncodeEnvelope` from the hash-pinned production kit is used in
the only guarded send call, then the actual decoder at test sinks. UTF-8 JSON, one envelope
per <=4096-byte datagram, U63 quantities represented as exact decimal strings. No new framing,
binary codec, float32, normalization, locale conversion or numeric round-trip is added.
Finite binary64 position/orientation including signed zero, subnormal, full decimal precision
and quaternion sign round-trip bit-exact in tested allowed cases. Infinities and signed/payload
NaN remain prohibited by the existing codec; they are tested as rejection, not exact-copy claims.
Source locate timestamps/domain/IDs, validity, world/source/calibration/mapping identity and
publisher epoch/session/sequence preserve the established schema semantics.

Tests exercise native local NTFS durable storage, real source/Owner/stamper/codec, faithful
nonblocking fake, and existing real localhost UDP. Manual reservation/completion, storage
barriers and explicit lifecycle calls deterministically choose pre/post-entry boundaries.
The real socket receive test polls/yields until a five-second watchdog because OS delivery
cannot be manually scheduled; observation of the actual packet is its condition, no fixed
sleep or publication-success shortcut. Actual peer receipt in that test is separate evidence,
not an application ACK or generalized peer delivery guarantee. Previous frozen regressions,
actual MonakaVR receiver replay and 4,285 protected SHA256 files remain mandatory.

Production-default sender activation, production endpoint/output, SteamVR/SlimeVR live output,
deployment, HIL, pairing, drivers/config/firewall/service/global settings remain forbidden and
unchanged. Production use needs an explicit reliability/loss/reordering/identity policy; this
software phase does not activate it. Store durability remains tested Windows/local NTFS
process-crash boundaries only; physical power-loss durability is not established.
