# ROTATION_ONLY downstream software audit

Audit date: 2026-09-22. Starting branch: `refactor/monaka-layer-separation`.
Starting HEAD: `1468432893505940d3953199e27e8829e45e9cda` (origin matched after fetch).
Upstream VIVE HEAD: `e8ecd513391ab4f15af2941380631e7f05049bea`, parent
`ffcb8946f2f68c90a03ea25e0492e915f2c17790`. VIVE was read-only in this audit.

## Evidence and scope

The tracked VIVE `docs/hil/vive-tracking-status-semantics.md` records a physical
Common Observation NONE -> FULL -> ROTATION_ONLY -> FULL run on 2026-09-22.
That later run did not co-record vendor raw status; its summarized quaternion
deltas are reported HIL observations, not measurements reproduced by this audit.
The separate tracked raw pose log supports the earlier status interpretation.
Only 02/12, 03/13, 04/14 are normalized explicitly; the `0x0f` operation used for
RF slot extraction is unrelated to pose-status decoding. No general status mask
or vendor code was added to Bridge.

Both downstream repositories pin actual wire-v2 kit SHA256
`a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3`, manifest
`64488a4194166554b7f07cab083272d9f7a684cffa3256d7650e841f81556ee8`, source
`572e58cfa20b8b4335207ea5dbcb3f04c587ddff`. ZIP, content manifest, lock and
contract checks run during configure. Historical C1 fixtures are not v2 evidence.

## Findings

- Registry sequence, first-admission age, lease and session rules do not branch
  on modality. Accepted ROTATION_ONLY packets refresh pose time; duplicates,
  reorder and metadata do not. Peer is not persistent identity.
- `calibrate` converts valid orientation for non-NONE samples, applying the
  selected quaternion permutation, normalization, world and mount rotations.
  Position, linear velocity and acceleration require FULL. A frozen numeric
  position in a ROTATION_ONLY packet remains diagnostic only.
- The output is ROTATION_ONLY/degraded with null position, false position
  validity, zero position confidence and positive orientation confidence.
  Backend source/device/session/sequence and orientation evidence survive in
  `input`; publisher/source/logical tracker remain separate.
- Angular history and quaternion sign continuity survive FULL -> ROTATION_ONLY
  -> FULL. NONE, stale input, session replacement and reconfiguration clear
  history intentionally. No synthetic orientation is recovered from NONE.
- `MtpTrackerState.modality` is informational and may lag pose-only transitions;
  the consumer must use the pose, as C2 requires. It never grants freshness.
- Bridge freshness expires at age >= 500ms by default; MonakaVR's existing
  freshness policy marks stale at age > 500ms. The one-nanosecond boundary
  distinction is preserved; both stop using rotation after transport timeout.

No production defect in the audited modality path was found. Production source,
wire, coordinate math, runtime settings and examples are unchanged.

## Added coverage and validation

`rotation_only_downstream` uses one Bridge instance and the fixed codec in both
directions. Synthetic Common Observation packets cover FULL -> ROTATION_ONLY
-> FULL -> NONE, 60 successive rotation samples, nonidentity profile/world/mount,
q/-q continuity, derivative history, null position/linear derivatives, confidence,
provenance, output sequence, duplicate/reorder/metadata age, timeout, session
replacement and retired-session rejection. Both MTP fanout channels are decoded.
The profile is explicitly synthetic: it does not approve or replace `vive-hil-v2`.

Windows x64 / MSVC, isolated source copy, Release configure/build: PASS.
Full CTest: **18/18 PASS**, including Direct mock and separate-process UDP.
The running HIL binaries and original config/status/logs were not overwritten.
No new hardware, HIL or SteamVR interactive test was run: **NOT RUN**.

## Local integration gate

Read-only local config inspection found Bridge mapping revision 61 maps
`vive-local-1 / 23:34:e4:5a:fe:39` to `altra-1`, whereas the committed MonakaVR HIP
assignment is `altra-0`. A logical-ID mismatch is unassigned, not fallback.
The local Bridge config is not committed here; its inspected SHA256 was
`d013d9d9fbfd4d3039d1d00bc060342da122aabe9d6b2a04360873e373465e8d`.
This is an inspection-time finding, not a durable hardware identity claim.

Before downstream HIL, select the intended current logical tracker explicitly.
Do not infer it from a slot or RF packet index, change approval on reconnect, or
copy the local HIL profile into defaults. MonakaVR's
`docs/rotation-only-downstream-audit.md` contains the diagnostic capture procedure
and remaining physical gates.
