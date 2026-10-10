# Common World production receiver (5AI-R2)

This independently packaged JVM-17 receiver is in MonakaBridge's phase branch.
It uses the pinned production 2.2 JVM kit, and the two extracted 5AB receiver
Core classes from MonakaVR `299dda9d31d98ac28da018c5be2a916e36193566`.
Their original algorithms are preserved outside marked inactive-recovery APIs;
`receiver/provenance.json` and `tests/receiver_scope.py` verify that boundary.
The sender, trusted live source producers, MonakaVR/SteamVR ingress registrations,
and all previous codec/golden vectors are unchanged.

## Production path and configuration

UDP/IPv4 -> one bounded datagram (4096 maximum; 4097 receive buffer detects
truncation/oversize) -> actual `MonakaCodec.decodeEnvelope` from the pinned
2.2 kit -> explicit association validation -> existing Common World Core.
There is one blocking NIO INET socket worker and no receive queue, automatic
DNS resolution, wildcard address, random production port, or test-sink fallback.
Malformed framing/UTF-8/required identity, duplicate JSON keys, truncation and
unsupported versions produce structured decoder rejections before Core calls.

The artifact contains a private Windows x64 Java-17 runtime, dependency JARs,
receiver JAR, licenses, build identity and inactive template. No-argument startup
prints a Created snapshot and exits without reading metadata or binding a socket.
Copy the inactive template outside the artifact, set an explicit IPv4, nonzero
UDP port and absolute receiver-only association path whose parent already exists.
`enabled` remains false; explicit local `start` is required even in control mode.
Invoke `receiver.cmd --control <absolute-config-path>` for line-delimited strict
JSON commands on stdin. This local control input is never a UDP packet decoder.
EOF or `shutdown` closes and joins the socket worker.

Commands: `status`, `associate` with `owner_id`, `world_space_id`,
`source_publisher_id`; `authorize-session` with Bridge envelope `publisher_id`,
canonical `session_id`, `clock_id`; `start`, `stop`, `restart`, `rebind` with
`bind_ipv4`/`udp_port`, and `shutdown`. The operator must authorize the Bridge
session explicitly; packets cannot authorize themselves. Bridge publisher/owner
and canonical upstream source publisher are distinct existing contract concepts.
No clear/reset API is provided. Changing retained trust requires an independently
reviewed destructive metadata workflow, never any normal lifecycle command.

Created -> Configured -> Bound -> Receiving -> Stopping -> Stopped; Shutdown and
Faulted are observable. Socket errors are class/code diagnostics without arbitrary
exception messages. Stop invalidates the generation before closing the socket,
disconnects Core authority and joins outside the state lock (5-second bound).
Decode/apply completions from retired generations cannot apply. Restart/rebind
keeps the association and requires fresh authorized session plus full snapshot.
A shutdown timeout faults closed and prevents a new socket worker.

## Association and legacy packets

Retained identity is exactly the wire-decoded tuple
`owner_id / coordinate_space.id / source_publisher_id`. Equality is opaque,
case-sensitive and does not normalize, infer or supplement publisher identity.
Epochs, session/generation, sequence and source IP/port are not association keys.
Initial `associate` is explicit and persisted; a valid packet cannot associate.
Identical repeated association is idempotent; replacement is denied.
OwnerMismatch, WorldMismatch and SourcePublisherMismatch return before Core,
retained state/high-water or history persistence can be changed. Packet counters
and rejection records are the only updates for a mismatch.

Legacy `monaka.common_world` 2.1 still decodes using the unchanged production kit.
Its publisher property is null even if an unknown optional field has that name.
The publisher-aware production acceptance path rejects LegacyPublisherUnavailable.
No fallback decoder or fabricated publisher is provided. Binary64 precision and
source/common field equality are tested before Float projection into existing Core.
Existing JVM parsing converts negative numeric zero to positive zero in both 2.1
and 2.2; R2 preserves and explicitly tests that baseline behavior. It makes no new
signed-zero encode/decode bit-preservation guarantee and changes no Protocol code.

## Persistence and restart

Receiver association metadata `MCWRXA01` version 1 is separate from receiver
ordering history `MCWRXO01` version 1 (`.order`) and from sender MCWREV01/02.
Both use magic/schema version, bounded length, explicit UTF-8 strings and a trailing
SHA256 over the payload. Canonical boolean bytes and field/reference consistency
are checked. This detects corruption, not malicious authenticated modification.
An exclusively owned `.lock` sidecar prevents concurrent metadata writers. Once
history exists, `.order-required` prevents a missing history file being mistaken
for a fresh receiver. Existing missing/corrupt/incompatible metadata fails startup
closed; no default association, deletion, repair, migration or temporary-file
promotion occurs. Each update forces a temporary file then requires atomic replace;
unsupported replacement fails closed. The two files are not one cross-file
transaction; the marker is written first so interruption may deny service instead
of guessing history. An association written before any session exists is safe.

Completed writes survive tested orderly runtime/process restart. Recovery retains
the exact association, world revision high-water, historical world/mapping snapshot,
retired epochs/calibrations, publisher sessions and source history. All formerly
live world/calibration capabilities and publisher sessions become retired; recovered
snapshots are explicitly inactive. A fresh authorized session must publish a strictly
higher world revision and fresh epoch/calibration before active state resumes.
The same tuple is accepted; incompatible tuples remain rejected. Observation receipt
time is never refreshed by restart. Complete external deletion of all metadata and
sidecars, hostile file modification and sudden power-loss durability are not proved.
No receiver application durable commit or acknowledgment protocol is introduced.

## Order, snapshots and observation boundaries

Existing Core requires seq0 world, seq1 mapping, then contiguous ordered events.
An identical last-sequence packet (sent_at excluded) is ignored as duplicate;
lower sequence is old/out-of-order and ignored. Same-sequence semantic conflict or
any sequence gap closes all live trust immediately; late missing events cannot
repair that session. Revision jumps are permitted if the event sequence is complete;
world replacement first requires ordered revocation. Lower/rejected revisions never
lower accepted high-water. Mapping revision high-water is scoped to its world revision;
new higher worlds start a new mapping scope. No tombstone eviction or wrap occurs.

Snapshot schema `monaka-common-world-receiver-snapshot-1` reports build/input hash,
runtime instance/time, lifecycle, configured and actual endpoints, socket generation,
restart count, wire versions/errors, exact retained tuple, metadata/history schemas
and persistence result, accepted world/mapping high-waters, packet digests/revisions,
accepted/rejected/duplicate/old/gap/legacy/stale-completion counters and faults.
Historical snapshots and current authority flags are distinct. Packet payloads,
metadata paths and arbitrary exception messages are absent.

DatagramReceived means a datagram reached the current socket worker. WireDecoded
means the production decoder succeeded. AssociationValidated means the decoded
tuple exactly matched retained trust. StateApplied/accepted means Core accepted
the ordered event locally (including revocation/unavailable); it does not imply a
fresh usable pose, application ACK, durable receiver commit or sender exactly-once.
Sender delivery semantics remain ordered best-effort local submission.

## Validation and pending work

`python scripts/receiver_build.py test` builds offline and runs deterministic fakes,
coordination-latch races, isolated metadata and real localhost UDP tests. CTest keeps
all 46 entering Bridge gates and adds receiver scope/runtime gates. No-argument
artifact smoke checks are inactive; production artifact contains no fixture/fake
classes. Replay validators and old loopback/test sinks remain separate.
5AI-S trusted live source producer and 5AH-E2 environment rebinding remain pending;
5AH-B controlled HIL stays stopped. No real endpoint, devices or OS changes occur.
