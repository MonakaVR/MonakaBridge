# Phase 2B-5AD: persistent revision store and dormant runtime adapter

Frozen Bridge contract: `eacb84580ce59666dafdbdb706daba1a302e1be3` (5AC), descended from
`be97d2279ffd51ba6e66bd9bc19526164309691d` (5AB). Their owner, publisher, coordinator, source
receiver and public receipt APIs are unchanged. The exact wire 2.1 kit remains unchanged.

## Composition and authorization

```text
explicit installation/world identity + local revision file
  -> open: high-waters and provenance ONLY (no world, mapping or publisher session)
  -> acquireAuthorization(fresh opaque Id): persist unique runtime lifetime
  -> fresh CommonWorldAuthorityOwner + separate CommonWorldPublisher (silent by default)
synthetic source bootstrap -> owner pending request -> value-only persistence ticket
  -> persist outside runtime lock -> write/flush/atomic rename -> exact durable receipt
  -> deliver under runtime lock AND nonblocking store authorization gate
  -> same ticket bytes + current stored bytes + current authorization lifetime/generation
     + current owner pending request/world/source/mapping
  -> unchanged 5AC owner -> fresh world/calibration epochs -> optional ordered DRY-RUN vector
```

`PersistentCommonWorldRuntime` is an isolated adapter, never instantiated in `bridge_service`,
UI, driver or another production composition. No live source, sender, transport, worker, deployment
or HIL is added. `beginDryRunPublisherSession` is an explicit in-process test seam; default publisher
has no session. Existing latest-only Fanout is never involved. Returned vectors remain the caller's
synthetic evidence, not a queue or delivery capability.

Construction reserves an explicit fresh authorization Id durably before constructing the owner.
The store retains all authorization Ids (128 maximum), never evicts and rejects reuse. Across restart,
old requestId=1 may recur, but its authorization lifetime cannot recur. World/calibration factories use
that reserved Id plus disjoint `.world.N` / `.calibration.N` namespaces and nonwrapping counters.
These tokens are independent of world/mapping revisions, time, PID and source authority. The unchanged
5AC token validation/retention still applies. Oversize/invalid derived tokens fail closed in the owner.
No production identity/epoch provisioning policy is supplied or activated by this phase.

Preparing a ticket neither writes a revision nor mints a world/calibration epoch. Transition revision
allocation uses the maxima of actual stored successor high-waters and 5AC requested minima; UINT32
exhaustion fails closed. Source headers are deliberately excluded from persistence matching because
5AC exactRequest compares payload facts, not publisher session/time. No saved pose is replayed on commit.

The exact receipt names a candidate's operation, runtime authorization Id, every 5AC request fact and
both committed revisions (or complete mapping/world/source/transform facts). Receipt values are not
capabilities: the store reads the committed record again under its gate before owner receipt acceptance.
The 5AC `persistentCommitConfirmed` booleans are set only there. A forged receipt or ticket, retired request,
changed identity, stale epoch/generation, delayed older commit or conflicting payload produces no activation.

The runtime mutex is the revocation linearization point. Revoking world/mapping or disconnecting source
invalidates outstanding ticket generations immediately; no disk lock or I/O is needed. Persist takes
the runtime lock only to check its ticket before I/O, then releases it. In-flight persistence may complete
and advance durable high-waters after revoke/dispose; its delayed receipt cannot restore authority.
Deliver/source/dry-run bootstrap use a **nonblocking** store gate while holding the runtime mutex. A busy
store cannot hold that mutex waiting for flush: deliver stays silent and can be retried. Source/publisher
operations with a busy/superseded authorization fail closed, explicitly invalidate source coverage and
revoke any world. A fresh source session/transition is then required. This conservative contention policy
is deliberate, not a promised nonblocking filesystem read: OS metadata/read latency can still delay a gate.
Caller serialization and non-reentrant callback/token rules of 5AC continue to apply.

A corrupt/unreadable committed record or disposed store detected at a runtime gate invalidates cached
world/mapping/source authorization before propagating the error. Repairing bytes does not revive a
retired ticket; a new source session and durable transition are required.

Store mutex serializes open/acquire/commit/receipt gate/dispose; the short gate callback shares that
serialization point with handover. Another runtime's acquire supersedes the prior authorization before
new events can become eligible. Old runtime values may remain in an earlier snapshot, but every new
publication operation checks the current gate. Revoke and returned immutable dry-run vectors are values;
previously returned vectors are never a newly authorized live send.

## Backend and crash protocol

Repository deployment is Windows x64. A narrow native file backend uses existing Win32 facilities and
BCrypt SHA256; no database or platform migration. Caller supplies an **existing**, exclusively controlled
directory on a local fixed NTFS volume. Remote/removable/non-NTFS and non-Windows backends fail closed.
No changes to legacy Config/saveConfig, driver/config files or deployment paths are made.

The sidecar `<file>.lock` is opened with share mode zero for the backend lifetime. Cooperative instances
using the same canonical path cannot open concurrently; failure is immediate. The OS releases the handle
after process exit. One shared store supports concurrent runtime calls; its mutex is the serialization
point. The persistent empty lock file is harmless. Caller must avoid directory junction/alias paths,
symlink/hostile directory replacement or external tools editing the store: this is an integrity/recovery
boundary for trusted local storage, not a hostile writer/security boundary. SHA256 is not authentication.

1. Read and validate committed `<file>` under the mutex; reject invalid records/identity/floors.
2. Check current authorization, exact duplicate and ordering. Same exact latest candidate is idempotent;
   same world or mapping revision with different facts, stale revisions or epoch conflicts cannot overwrite.
3. Write complete next record to sibling `<file>.tmp` (two writes expose deterministic partial-write seam).
4. `FlushFileBuffers` the temp handle; close it. Recheck cancellation before publish.
5. Same-directory `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` publishes the full record. No cross-volume
   copy flag is used. Exact target name is the sole commit marker. No candidate/temp is a restore source.
6. Only after successful publish return the exact value receipt. A post-publish failure throws without
   a receipt; reopen/read resolves whether the new revision committed. Retry of exact committed bytes
   is idempotent, while any caller whose lifetime/request was retired must create a fresh transition.

Cancellation before publish throws without a new commit. Once rename succeeds, cancellation cannot undo
it: success receipt may return despite a newly set cancellation flag. Post-publish hook/API uncertainty
throws; never guess success. Exceptions clean up the exact sibling temp created by the store. Abrupt
termination may leave temp behind; reopen ignores it, next locked write replaces it. A disappeared
previously observed committed file fails closed. Missing store on initial open is explicitly new storage
seeded by the supplied high-water floor, never evidence that an old installation is safely reset.

Local NTFS same-volume atomic name replacement is the backend assumption. Deterministic exceptions and
actual child-process termination test old-or-new complete visibility at each boundary. **Power-loss
durability is NOT established**. Temp data is flushed and WRITE_THROUGH is requested for rename, but there
is no implemented/tested directory fsync, disk-controller guarantee, journal recovery proof or physical
power-cut test. Network filesystems, VM rollback, snapshots/manual rollback and loss of the complete store
are outside this guarantee. Production recovery of lost storage requires external installation identity/
high-water recovery policy; silently initializing a known old installation is not authorized.

## Binary schema version 1

All integers use fixed-width little-endian bytes. Strings use U32 byte length and UTF-8 codec-validated
payload strings. Max record 65,536 bytes, max string 4,096, max authorization history 128. Whole payload
is followed by 32 raw SHA256 bytes. Unknown header/version/operation, invalid flags/lengths, trailing bytes,
truncation, invalid schema facts or mismatched SHA256 fail closed. Bad committed files remain untouched
for diagnosis; no older backup fallback or automatic reset creates rollback/high-water ambiguity.

```text
"MCWREV01"; U32 version=1
ownerId; worldSpaceId; U8 worldRevisionPresent; U32 worldRevision; U32 mappingRevision
currentAuthorizationEpoch; U32 historyCount; historyCount * authorization Id
U8 latestPresent
if latest: authorization Id; U8 operation; U32 worldRevision; U32 mappingRevision; operation payload
SHA256(all preceding bytes)
```

Transition payload: U64 nonzero requestId; ownerId; worldSpaceId; exact anchor; U32 minimumWorldRevision;
U32 minimumMappingRevision; reason. Anchor: source_id, source_authority_session_epoch, source_space_id,
U64 nonnegative signed-63-bit generation, source_space_kind, source_time_domain_id. Header facts are absent.
Mapping-update payload: CommonMappingReference (complete CommonWorldReference, SourceSpaceAuthority,
calibration_epoch, U32 mapping_revision) and RigidTransform. Mapping-reestablishment payload: complete
CommonWorldReference, anchor, U32 mappingRevision. World reference preserves owner_id, world_epoch,
coordinate_space id/convention/U32 revision. Mapping transform stores seven **raw binary64 bit words**,
never decimal JSON, float32, arithmetic or locale parsing. M2 accepts the unchanged exact positive identity;
signed-zero transform conflict is rejected rather than silently normalized. Poses stay in the unchanged
5AC exact-copy path; nonfinite input remains rejected by the existing wire contract.

Committed record high-waters must equal its latest candidate's revisions. Its candidate epoch may be
retired after a new authorization reservation; recovery returns provenance/high-waters, never rights.
RequestId is scoped by durable authorization Id; revision identity is installation/world/worldRevision/
mappingRevision plus tagged payload. Mapping-only commit keeps world revision exact and increases mapping
revision. New-world commit increases both persisted high-waters. Duplicate comparison normalizes only
contract-excluded headers/attestation booleans, never identity or binary64 bits.

## Validation

`common_world_persistence_tests` uses native temporary stores, injectable boundary hooks, deterministic
pauses, concurrent calls and actual terminating subprocesses. Existing owner/publisher tests and generated
replay fixtures are unchanged. `verify_common_world_persistence_dormancy.py` checks production isolation,
5AC core equality and protected paths; the original 5AC static suite still applies unchanged.
The execution report records commands, exit statuses, actual counts, failure-mode table, replay, preserved
5S hashes and platform/power-loss limitations. No test result authorizes live transport or a subsequent phase.
