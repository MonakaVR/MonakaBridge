# Phase 2B-5AH-A2: retained runtime control and continuous trusted input

Fixed base: `2cf9a3d530de85fccf6b5e200946b3bd81b73572`.
Phase branch: `feature/common-world-hil-control-observability-live-input-v2.1`.
The previous 5AH-B readiness-blocked evidence remains immutable. A2 is software-only.
After A2 passes, restart 5AH-B at the NEW artifact identity gate; never reuse 5AH-A binaries.
Delivery semantics remain **ordered best-effort local submission**.

## Production lifecycle and retained approval

Ordinary no-argument and legacy OFF startup remain inert. Explicit `--control` starts a
retained Windows local inherited stdin pipe host. It has no socket listener, wildcard
bind, installation, service, driver, credential, or global configuration dependency.
Use the reviewed binary, a complete explicit production config with `enabled:false`,
and an explicit emergency sentinel path whose parent exists. Configuration stays OFF.

```
monaka_common_world_runtime.exe --control CONFIG --emergency-stop-file CONTROL/EMERGENCY_STOP
```

The host emits a `created` JSON line containing the current snapshot. Every subsequent
command must name its `runtime_instance` and `control_session`; a client attached to an
old process cannot operate on a replacement. These local process binding identifiers
are not network authentication credentials. The operator protects the inherited pipe
and trusts its producer. The host never binds a remote control interface. Machine
commands use JSONL on stdin and single-line JSONL responses on stdout; no console UI.

Lifecycle: Created/DisabledByDefault -> prepare -> ArmedDisabled -> refresh-readiness
-> explicit activate -> Active -> deactivate -> ArmedDisabled. Source loss/reconnect
enters OwnerUnavailable. Explicit fresh source authorization and durable successor
commit can return ArmedDisabled. Owner revoke is terminal for owner eligibility within
this runtime. Emergency revoke and shutdown are terminal. Each runtime instance, owner
epoch, source callback generation, producer session, publisher generation and transport
generation is issued by its existing lifecycle, never assigned by the HIL caller.

`prepareArmed` validates the complete OFF config, acquires the real store epoch, runs
startSource -> authorizeSource -> commitSource and attaches an inert staged transport.
The sender is disabled, the queue is empty, no production socket is connected, and pump
returns activation-inactive. The same runtime and owner remain alive for approval.
The legacy direct activate API refuses ArmedDisabled; current bound readiness is required.

The activation linearization remains `acceptCurrentCommit` around staged transport
activation after current owner/source/world/mapping/config checks and
`connect -> enablePublisher(exact receipt)`. Publication itself revalidates CURRENT
receipt and source/owner context at the existing final sender store gate for every send.
Source persistence does not hold the orchestration mutex; revoke can retire an in-flight
preparation/authorization and its generation fence rejects the later result. Pre/post
snapshots may show another operation interleaved across persistence and remain labelled
with exact runtime/generation and operation IDs rather than pretending an atomic commit.

## Readiness freshness

A new opaque readiness token is an in-memory observation binding to this runtime,
control session and generation. It cannot authorize a different instance. A second
refresh replaces the old token. activate requires `explicit_enable:true`, the exact
current token, ArmedDisabled, and CURRENT gate checks. The token is consumed on attempt.
No offline 5AH-A preflight result is accepted. Snapshot/status never grant authority.

| Change | Invalidation/revalidation |
|---|---|
| Owner revoke/change | Lifecycle generation and token retired; current owner checked |
| External store epoch/revision/reset | Exact current durable receipt gate; snapshot readiness false |
| World/source authority/identity | Current source/owner/world/mapping gate; receive retires token |
| Source callback generation/session | Reconnect retires token, generation and source lease; explicit authorization required |
| Config/endpoint/transport/codec/store path | Frozen exact config bytes AND file version checked; reject changed config; new process required |
| Config enabled bit | OFF must remain frozen; explicit activation is a separate command |
| Transport/publisher generation | Reconnect/deactivate retire old queue/attempt and token |
| Emergency/shutdown/restart | Terminal lifecycle and new runtime/control identities |
| New refresh | Generation and token replaced |

Config changes observed by the host cause emergency revoke even during no-data. API
activation/input/pump also check the frozen configuration. No in-place owner/endpoint
reconfiguration or store reset command exists. Future changes require a new process,
new epoch and fresh preparation/approval. Readiness is not a reusable bearer grant.

## Commands

Common request fields: `operation_id`, `operation`, `runtime_instance`, `control_session`.
Input/authorization additionally name current `source_lease` and `source_generation`.
Trusted wire envelopes are objects in `envelope` and go through the pinned v2.1 codec.
No caller-supplied owner epoch, world revision, publisher eligibility or transport ID
is accepted. Unknown/duplicate/malformed command fields reject without raw input logging.

| Command | States, production path, result |
|---|---|
| status / snapshot | Any; observation only; unchanged generation/control metadata |
| prepare | Created, complete OFF config and trusted bootstrap; prepareArmed -> normal owner/store/source construction; ArmedDisabled; already-prepared rejects repeat |
| refresh-readiness | ArmedDisabled; CURRENT gate, replace observation token; nonready rejects |
| activate | ArmedDisabled; bound token + explicit_enable + CURRENT activate gate; consumed token cannot repeat |
| deactivate | Nonterminal; disablePublisher -> disconnect, retire queue/attempt; ArmedDisabled; idempotent while disabled |
| owner-revoke | Nonterminal; normal revokeOwner; terminal owner approval retirement; repeats safe |
| emergency-revoke | Any; emergencyRevokeCommonWorld/revokeOwner first -> terminal shutdown; repeats safe |
| source-reconnect | Prepared nonterminal; stopSource -> startSource; fresh callback generation + lease, owner NOT approved; never enables publication |
| source-authorize | OwnerUnavailable, current lease/generation, owner not explicitly revoked; authorizeSource fresh publisher bootstrap -> commitSource exact durable successor; ArmedDisabled, still OFF |
| source-disconnect / source-eof | Prepared; stopSource; explicit producer loss, retained runtime; no implied reconnect |
| input | Current lease/generation; receiveCurrent -> existing source receiver/authority validation; structured admitted/rejected result |
| transport-reconnect | Nonterminal; CURRENT activation/owner/world/receipt/publisher checks; retire -> guarded activate with fresh publisher/transport session when Active; OFF stays disabled |
| pump | Active only; existing guarded ordered sender API; local submission result only |
| shutdown | Any; revoke publication/source/owner before disposal; repeats safe; later activation rejects |

Every result uses `monaka-common-world-control-2`: operation_id, operation, accepted,
result, before, after. Rejections preserve current state except explicit fail-closed
retirement. A stale control session cannot mutate runtime/control metadata. Unsupported
lifecycle operations produce structured rejection. Logs contain no raw config/command
or credential fields.

## Continuous input and reconnect

The pipe is a continuous trusted input/control contract, not a finite source file. It
may remain empty indefinitely. `PeekNamedPipe` distinguishes no-data from producer/pipe
EOF and lets the emergency sentinel be checked without blocking on source getline.
Partial commands are buffered with a 128 KiB bound and depth 16. One envelope retains the
existing codec 4096-byte bound. Pipe EOF terminates source and runtime; explicit source-eof
retains the runtime for later reconnect. No automatic source or transport reconnect occurs.

The producer is responsible for physical trust and envelopes. This repository supplies
only deterministic software fixtures, not a real device producer. First authorization
requires matching publisher, fresh producer session/clock and sequence zero authority.
Contiguous order, duplicate/conflict rules, publisher identity, source token/generation,
source clock and observation high-water remain the original TrustedHmdSourceStream rules.
Source sequence starts at zero ONLY in a fresh explicitly authorized producer session;
retired producer sessions and source history remain retained. Old callback input is
rejected before envelope admission by source lease/generation. Source reconnect alone
neither grants owner approval nor commits a world. Explicit authorization may create a
new world revision and requires fresh readiness plus later explicit activation. This
makes no same-world seamless recovery promise; future receiver association policy is
an operator-supplied HIL prerequisite. Store state and owner epoch are never reset.

Transport reconnect retires pending attempts, old transport generation and publisher
session. Active reconnect revalidates current authorization before and after retirement,
then obtains a fresh ordered publisher bootstrap through the same activation gate.
Late completion cannot update current queue/cursor/submission record. Reconnect OFF only
retires the disabled session, without opening a production socket or enabling transport.

## Stable snapshot schema

`monaka-common-world-runtime-snapshot-2` contains phase/commit, runtime/control identity,
Unix UTC milliseconds and local monotonic nanoseconds captured with the state copy
(and preserved across delayed serialization), gate state/generation/reason,
configuration activation flag, shutdown/recovery, readiness token/generation/validity,
owner epoch (direct session/store observation, not token parsing), source lease,
owner/emergency revoke flags, last activation transition timestamp, last control ID/result,
last accepted/rejected source event result/sequence/timestamp, source result and reconnect result,
mode/codec/transport class, hashed endpoint/store path, and delivery semantics.

`store` records Observed/NotOpened/Busy/Unavailable, recovery result, CURRENT authorization
epoch, latest exact durable world/mapping revision and receipt identity/operation,
plus whether that observed receipt matches this runtime. A
read failure yields Unavailable; tryObserve returns Busy while a durable write owns
the store lock so machine owner/emergency/shutdown control never waits for that write; no cached high-water is treated as current authorization.

`sender` records enabled/connected/disposed/pending completion, publisher/transport
generations, queue depth/capacity/high-water/head attempts, source generation/activity,
owner approval/persistence busy/runtime state/pending transition, current world identity,
source authority identity, direct owner epoch, actual selected publisher/session/clock,
source type/lifecycle/sequence high-water, current authority envelope and last valid
observation envelope with receive timestamp, publisher/sender/transport states,
last accepted sequence, and local submission record. Fields not yet applicable are null.
No secret configuration bytes or plaintext endpoint/store path are emitted.

The local submission record is captured at entry into the EXISTING guarded nonblocking
transport API, then classified from that call or a CURRENT session completion. Fields:
owner epoch/ID, world epoch/ID/revision, mapping revision, source publisher/ID,
transport/publisher generation, monotonic ordering position, wire sequence, monotonic
timestamp and local result. Stale completions cannot change it. It is observational,
never part of authorization, never peer receipt or receiver commit. Earlier entered
submissions cannot be recalled by revoke/deactivate.

## Paired evidence and artifact

`scripts/capture_common_world_fault.py --response SAVED_MACHINE_RESPONSE --output NEW_DIR`
creates before.json, operation.json, after.json and pair.json exclusively. Pair identity,
schema, monotonic generation and same runtime/control session are checked; every payload
has SHA256. Existing/blocked evidence cannot be overwritten. LocalControlClient is a
reusable same-process pipe adapter and never auto-activates. A2's updated HIL template
records these pair references and source/readiness/transport/local submission fields.

Build/package with the established static CRT x64 production tool. The new release
includes this contract and capture tool; manifest records every payload hash, commit,
branch, build-input digest, runtime, architecture, default OFF and config/template hashes.
Software host validation never sends an activate command to the production registration.
All active/race/send tests use the separate existing deterministic test dependency
registration; no actual production endpoint, real receiver, device, HIL, SteamVR or
SlimeVR output, service, driver, registry, firewall or global OS change is involved.

## 5AH-B gap resolution and remaining environment

| Prior gap | Implementation / production path | Proof |
|---|---|---|
| G1 preparation exits or activates before approval; OFF cannot prepare live owner | Retained local host -> prepareArmed -> start/authorize/commit; bound refreshReadiness/activateReady | A2 C++ 01-21, host retained OFF/no-data tests |
| G2 incomplete current snapshots/local submission/fault checkpoints | Extended snapshot -> actual runtime/source/store observations; sole sender send-boundary record; paired control responses/capture tool | A2 C++ 22-23, 33, 49, 51-56; host pair; fault tooling |
| G3 source/transport/revoke controls not reachable from production host | Local command -> control -> production lifecycle APIs -> unchanged state machines | A2 C++ 25-35, 41-50, 57-58; pipe/sentinel/EOF tests |
| Continuous JSONL is static EOF-only | Local continuous input lease/generation contract -> pinned codec -> existing trusted source receiver | A2 C++ 27-30, 36-43, 46; continuous host tests |

Only future HIL operator/environment values remain: (1) approved IPv4:port, receiver
type/version and retained-world association policy; (2) canonical owner/world and trusted
source producer identity/type; (3) isolated new versus inherited store policy/path.
No A2 HIL result exists. No application ACK, receiver durable commit, exactly-once,
power-loss durability beyond prior process-crash scope or unmeasured end-to-end latency
is promised. 5AH-B remains stopped until its new artifact and real-environment gates pass.
