# Phase 2B-5AH-A — Deployment / HIL Readiness Foundation

Fixed base: `8abd5eb3e60e2e37592039488e72c77ecf63b6d1`.
Phase branch: `feature/common-world-deployment-hil-readiness-v2.1`.
This foundation adds a separate offline observer, packaged controlled host and local
artifact tools. Every 5AG behavioral implementation and test remains byte exact.
Delivery remains **ordered best-effort local submission**.

## Artifact and build

Use Windows x64, Visual Studio 18 2026/MSVC C++17, CMake 3.20+, Python 3.13+,
and the verified protocol/upstream artifacts already pinned in this repository.
Build with static MSVC CRT and SteamVR build disabled. No driver/runtime installation.
The build identity records the configured Git commit and a SHA256 over sorted source
paths and SHA256 contents. Reconfigure after changing source/commit. Package only a
clean committed tree; a stale binary identity or build-input hash rejects packaging.

```powershell
cmake -S . -B build/production -A x64 -DBUILD_TESTING=OFF -DMB_BUILD_STEAMVR=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build/production --config Release --target monaka_common_world_hil_runtime monaka_common_world_preflight
python scripts/common_world_artifact.py package --build build/production --output dist/common-world-REVIEWED-COMMIT
python scripts/common_world_artifact.py verify dist/common-world-REVIEWED-COMMIT
```

`bin/monaka_common_world_runtime.exe` is the new controlled file-stream host (CMake
target `monaka_common_world_hil_runtime`), composed around unchanged 5AG. It is separate
from legacy `monaka_bridge_service`, SteamVR and the original 5AG host. Ordinary launch
prints DisabledByDefault diagnostics, with no config load, store open or network I/O.
`bin/monaka_common_world_preflight.exe` is the offline observer. The archive includes
templates, dependency inventory, documentation, rollback/report/integrity scripts,
artifact manifest and its SHA256. Bridge/Owner/Store/Publisher/Sender/activation/codec
libraries and CRT link statically; Windows DLLs are inventoried from actual PE imports.
Packaging verifies PE x64 and rejects dynamic CRT imports. Protocol NOTICE and nlohmann
license accompany the static codec/JSON libraries.
No backend DLL, SteamVR/SlimeVR process, device or credential is required for preflight.
Python and PowerShell are operator tooling dependencies, not native host dependencies.

The manifest lists every payload file, size and SHA256 plus commit/branch/build input,
Release/x64/native framework and default OFF. Its detached digest is non-circular and
uses canonical ASCII/LF bytes on Windows as well.
The digest proves byte correspondence, not signer authenticity: 5AH-B must retain the
reviewed digest through a trusted handoff. Toolchain equality alone does not promise
bit-identical PE files across machines; reviewed artifact bytes are the HIL identity.

## Layout and configuration contract

Keep the release immutable. Place mutable/operator inputs in sibling directories:

```text
installation/
  releases/reviewed-commit/{bin,templates,scripts,docs,dependency-inventory.json,artifact-manifest.*}
  config/production.activation.json
  source/authorized-source.jsonl
  state/common-world.rev[.lock/.tmp]
  logs/
  reports/
  control/EMERGENCY_STOP
```

Create writable directories explicitly; preflight does not create them. Copy the
template into config while `enabled:false`. All blank identities and endpoint must
be filled before preflight can PASS. Required fields: enabled boolean false for
preflight, mode production, transport udp-ipv4, codec monaka-wire-2.1, canonical literal
`udp://IPv4:port`, owner_id/world_space_id/source_publisher_id, revision_path and peer
policy `explicit-authority-association-no-implicit-world-replacement`. Use no DNS,
wildcard, multicast, broadcast, link-local, loopback/test address or leading-zero octet.
Port is 1..65535. IDs use existing 5AG UTF-8 <=96-byte/no-control/nonblank rules.
Relative revision paths resolve against the config file directory.
Optional queue_capacity is integer 2..4096 (64 default); retry_limit is integer 1..32
(3 default), solely known-zero manual retries. No automatic reconnect parameter exists.
Unknown fields, duplicate keys and ambiguous syntax reject preflight. No secret fields
are supported. No build helper toggles activation, supplies a live endpoint or enables
an environment/registry/global fallback. Config remains the explicit 5AG contract.

The authorized source JSONL is the existing trusted typed adapter boundary. Its first
envelope must be the matching source authority, not an unauthenticated device discovery.
Physical source trust/freshness and device pairing remain 5AH-B operator prerequisites.

## Non-destructive preflight and readiness

```powershell
bin/monaka_common_world_preflight.exe --preflight ARTIFACT CONFIG SOURCE_JSONL LOGDIR REPORTDIR EXPECTED_COMMIT [EXPECTED_RECOVERED_OWNER_EPOCH]
```

JSON goes to stdout (exit 0 only on ReadyForControlledActivation); capture it to a NEW
external evidence file. This observer never sends, binds, connects or probes a target.
It checks full config even while OFF, artifact file/digest/build identity, Windows DLL
availability, existing writable state/log/report directories, local NTFS exclusive store
ownership, actual stored-record recovery and installation/world/optional epoch matching.
It decodes/encodes the actual pinned codec and constructs source/owner/ordered sender
against a **private volatile copy** of recovered bytes. A fresh prospective epoch and
readiness revision commit exist only in that copy, never in the real store. This proves
software construction prerequisites; it does not establish the live runtime owner.
The offline construction transport throws on connect/submit and is never selectable by
production config. The observer returns no receipt, activation token or sender handle.

Actual store access only takes/releases its existing exclusive `.lock`; temporary
exclusive write-access probes use delete-on-close. Committed state/high-water/epoch,
uncommitted `.tmp`, config and artifacts are never rewritten. These lock/probe side
effects are the defined non-destructive access check. Store ownership conflict fails
closed; do not use preflight concurrently with a running production owner.

| State | Condition / transition |
|---|---|
| NotConfigured | Missing explicit config or no command inputs |
| InvalidConfiguration | Bad syntax/unknown/duplicate/missing field, ON during preflight, wrong codec/identity contract |
| ArtifactInvalid | Missing/corrupt manifest, hash/version/build/x64/Release mismatch, unexpected file/path |
| DependencyMissing | Missing declared binary or required Windows DLL/dependency contract |
| PersistentStateNotReady | Local NTFS/open/lock/recovery/identity/epoch failure |
| RuntimeNotReady | Path/access/source/owner/publisher/sender prerequisite failure, busy/revoked/shutdown/input race |
| TransportConfigurationInvalid | Test/production mode mixing or malformed production endpoint/transport |
| ReadyForControlledActivation | Every software prerequisite checked; production remains OFF |

Concurrent/reentrant calls on one observer return busy without replacing an in-flight
result. Separate processes serialize actual store ownership via existing exclusive lock.
Repeated runs perform fresh checks. Config/source/manifest/persistent bytes are re-read
before result publication. Invalidation increments a generation; delayed checks cannot
attach after invalidate/shutdown/emergency. Shutdown/emergency are terminal. Snapshot
JSON is an observation at its monotonic timestamp and can become stale immediately.
**No activation entry point accepts readiness results.** Config/owner/epoch/world/source
changes invalidate their usefulness even when an external saved JSON cannot be erased.
5AH-B must start a fresh 5AG preparation and exact current authorization gate. The
unchanged source generation, owner epoch, persistence receipt, ordering/backpressure,
final send authorization and production wire gates remain mandatory.

## Diagnostics and emergency stop

The controlled host emits structured JSON on default/prepare/activation/shutdown status:
commit, monotonic ns, config enabled, gate state/reason/generation, shutdown/recovery,
mode/transport/codec, owner approval, world/epoch/space/revision, source generation,
pending transition, high-water, queue depth/capacity/attempts, publisher/transport
generations, enabled/connected/disposed/pending completion. Runtime owner epoch is
observed from the unchanged 5AD `epoch.world.counter` token layout, labelled as such;
it grants no rights. Preflight separately records
source session/clock/authority generation, recovered owner epoch and durable revisions,
manifest/build/config fingerprints, and clearly labelled construction-only state.
No arbitrary config bytes or secrets are dumped. Status never changes eligibility.

For later explicit activation the packaged host requires:

```text
CONFIG AUTHORIZED_SOURCE_JSONL --enable-production --emergency-stop-file EXISTING_CONTROL_DIRECTORY/EMERGENCY_STOP
```

**NOT EXECUTED IN 5AH-A.** With no explicit enable it only prepares (if config is ON),
reports ReadyButInactive and exits. OFF config exits before storage/source construction.
The host compares exact config bytes and checks the stop sentinel before activation,
each source receive and each pump. Config read failure/change or sentinel presence
calls local owner revoke FIRST, then terminal shutdown. The software helper is idempotent,
retires queue/pending completions, and cannot reactivate/reconnect the retired session.
Removing the sentinel cannot revive the same runtime. A new process/fresh preparation,
fresh owner epoch and explicit operator authorization are required. No transport reply
is needed. Already-entered local submissions cannot be recalled. Polling and mutex
serialization give no real-time kill latency bound; a blocked source file read may delay
polling. Operator process termination is the fallback kill for a hung process. It stops
new process publication but does not prove remote invalidation or receipt.

## Fail-closed matrix

| Failure/race | Result / activation boundary |
|---|---|
| Clean launch/template/absent config | Default OFF; no live transport |
| Invalid/incomplete config, test mode, ON preflight | Reject before store/owner construction |
| Artifact/dependency/path/report failure | Explicit nonready; no activation call exists |
| Recovery/world/identity/epoch mismatch | No real persistent commit or transport |
| Source/codec/owner/publisher/sender construction failure | Volatile candidate discarded |
| Repeated/concurrent/reentrant preflight | Fresh observation or busy; no grant |
| Config/source/manifest changes in flight | Final byte comparison rejects |
| Shutdown/emergency/owner-context invalidation during check | Generation retired; delayed result discarded |
| Saved stale PASS JSON | Cannot enter activation; fresh 5AG gate still required |
| Queue/reconnect/pending completion during emergency | Revoke first; terminal shutdown; no new eligibility |
| Rollback unexpected file/state or reparse path | Removal refused; preserve state/config/logs |

## Controlled 5AH-B sequence — activation NOT EXECUTED IN 5AH-A

1. Copy reviewed artifact; verify manifest digest against trusted reviewed evidence.
2. Create external directories; copy config template; verify enabled false and no secrets.
3. Enter reviewed identities/source adapter/target and NTFS state path; retain OFF.
4. Run preflight into a new report; verify PASS state, commit/input/hash/dependencies.
5. Confirm actual physical source readiness, peer authority/reset policy, recovered
   durable revisions/installation, expected world/identity and transport target.
6. Ensure emergency sentinel absent and its parent writable; select rollback artifact.
7. Operator explicitly sets config enabled true and launches packaged host with the
   explicit enable and emergency stop path. **NOT EXECUTED IN 5AH-A.** Preparation creates
   a fresh live owner epoch/durable successor; preflight's prospective values are unusable.
8. Observe 5AG transition/diagnostics; verify downstream data path by HIL evidence.
   **NOT EXECUTED IN 5AH-A.** Local acceptance alone is not receiver/application commit.
9. Exercise separately controlled source/transport loss/restart/emergency scenarios;
   retain exact artifacts and UTC + monotonic timestamps. **NOT EXECUTED IN 5AH-A.**
10. Explicitly deactivate through 5AG for an embedded caller, or end the file stream /
    set emergency sentinel for the packaged host. Verify terminal Shutdown, zero queue,
    disabled publisher/disconnected sender; terminate a hung process if needed.
11. Collect stdout diagnostics/logs/preflight/evidence; confirm OFF before a new process.
    Generate a new report with `scripts/new_hil_report.py --artifact ARTIFACT --output NEW_EXTERNAL_REPORT`.
    Fill the NOT_RUN template with actual observations and PASS/FAIL; never infer HIL.

## Rollback and cleanup

Stop the host; keep the prior reviewed release and its manifest. Ordinary rollback
switches operator launch paths to the prior release with activation OFF, then optionally
runs `scripts/rollback.ps1 -ArtifactPath EXACT_RELEASE` (dry run) and `-Apply` to remove
only the reviewed release. It checks resolved target/ancestors/descendants for reparse
points, every hash/path/inventory and rejects any unexpected embedded state/config/log.
It never enumerates unrelated sibling directories for removal. External config,
logs/reports and persistent `.rev/.lock/.tmp` remain. No persistent-state deletion command
is supplied: deletion requires a separate explicit destructive task, with the consequences
for revision high-water/owner identity reviewed. Do not downgrade/reset the persistent
format or reuse old exact receipts/epochs when changing artifacts. A predecessor unable
to recover current state is a blocked rollback, not permission to delete state.

## Software-only limits and evidence

Software tests cover deterministic invalid inputs, artifact/dependency/access/recovery,
identity/epoch, transport separation, repeated/concurrent check, shutdown/config races,
stale observation, emergency queue/completion retirement, NTFS preservation and rollback.
The phase report under `build/reports/phase2b5aha-deployment-readiness-20261010/` records
actual commands/counts, artifact hashes, 5S hashes and final local/remote/clean state.
All previous suites remain required; static allowlist extensions retain earlier assertions.

Executed 2026-10-10 JST: Bridge **41/41** (all prior 36 retained, 5 additions), new
readiness **55 cases / 713 assertions**, deployment tooling **11 cases**, unchanged
5AG **81/882**, Live Source **41/696**, Persistent Store **32/513**, Sender **41/838**,
Core **1,875**, Desktop **153** with zero failures/errors/skips, original actual receiver
replay **14 streams / 82 events**, plus sender **2 positive/10 events + 1 expected
rejection/6 events**. Protected **4,285 SHA256 / 8,376 inventory entries** unchanged.
Production Release/static CRT build succeeded; final source-bound package/remote identity
and its software-only acceptance evidence are recorded in the accompanying phase report.

No actual production endpoint, real HIL, device output/pairing/timing/latency, deployment,
service/driver, registry/firewall/global environment or production credential was exercised.
There is no application-level ACK, receiver commit, exactly-once delivery, durable
publication cursor or stronger delivery claim. Power-loss durability remains unproved;
persistence retains only the previously established Windows/local NTFS process-crash scope.
