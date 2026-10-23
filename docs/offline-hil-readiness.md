# Offline HIL readiness implementation

This is the authorized 2026-10-23 readiness successor to the frozen historical
work scopes. Physical tracking, actual SteamVR rendering and physical criteria
adoption remain separate operator gates. Never deploy test policies or replay
witnesses as physical approval evidence.

## Production output approval

The ordinary calibrator composes `makeProductionOutputApprovalAuthority` from
`OutputApproval` beside its executable. No CLI switch injects a test authority.
The factory does not initialize OpenVR. `review_output`, `prepare_output` and
`commit_output` revalidate the external policy, evidence hashes and current
configuration. The final before-replace guard rejects cancellation, changing
criteria, stale config and stale source/reference witnesses before CAS.

`config/output-approval` contains schemas and **disabled** templates. Policy
length units are metres; angular residuals are radians. Limits must come from
an adopted physical policy. No production numeric limits are supplied here.
Policy scope names source, input space, profile and world space. Evidence binds
the individual device, source session/clock, input revision, reference serial,
standing witness/generation, current World and mapping revision, exact config
preimage/candidate, native result/raw hashes, original timestamp and expiry,
criteria ID/version/hash and producing tool/build identity.

Use `scripts/prepare_output_evidence.py --help` to prepare a new review folder.
It copies native metrics, never recomputes them, never refreshes the measurement
timestamp and always writes disabled adoption. For older trajectory results,
`monaka_trajectory_replay raw.json original-result.json replay-result.json`
reproduces native W/E/counts/RMS exactly before exposing native max/all-pair
metrics. Review and pin the resulting evidence set. After operator adoption,
place the set beside the intended helper; enable policy/adoption only with
approved limits and validity. A new output-approval session must discover the
fresh current input/reference and review the saved current configuration.
World-session confirmation/review tokens cannot approve output.

The selected member changes only `space_approved`; mapping revision increases
once. Other members, profile/contracts, W, existing M and measured E are not
modified. A repeated request ID returns its cached result; a new commit request
after saving is rejected. A competing file writer is preserved by exact CAS.

## Result and diagnostics

Native `trajectoryResultJson` serializes inlier and all-pair RMS/max from the
solver. The managed model and GUI display those bytes without recalculation.
`first_failure` is sticky across monitor/owner/session responses, idle terminal
notifications, GUI faults and `terminal.json`. `SESSION_INVALIDATED` remains
the category. Result artifacts are immutable: a later failure is recorded in
the separate terminal artifact rather than rewriting a measured result.

## Read-only preflight and startup

`scripts/hil_preflight.py --package <package> --output <report.json>` checks
package hashes, compatibility receipts, disabled/missing policy, runtime/profile,
registry, registered driver copies, helper SDK identity, config, mirror-port
occupancy and current service identities. It only invokes the helper's
`--hil-runtime-path` static query; it never calls VR_Init, binds a port, starts
SteamVR, changes registrations, stops services or writes configurations.
The output report is the only intentional write. Missing evidence produces FAIL.

Normal startup is `Start_Bridge.cmd` / PICO `Start_Receiver.cmd`. The shared
launcher selects the newest successful compatible artifact set in
`build/artifact-sets.json` (or explicit `--catalog` / `--package`), verifies all
pins and receipts, and starts the existing tray. It never builds or deploys.
Tray reuse checks service/controller path, file SHA and creation time. X closes
only the management window. Exit requests graceful stop only for the owned
service before hiding the tray; stale PID/name and other owners fail closed.

## Direct presentation and offline tests

The ordinary driver now includes the existing reviewed D2 Rev3 resource and
explicit assignment implementation. Generic/unassigned devices keep generic
presentation. Assignment binds publisher/session, source/device/input session,
mapping revision and approved config. Lost identity/expiry revokes appearance
for that registered object's lifetime; a new explicit snapshot needs restart.
Rev3 is an approximate mesh; physical mount alignment is not certified.

After approval, `monaka_prepare_presentation config.json health.json source
device new-folder` prepares the explicit selection and byte-identical config
snapshot. Health exposes the current publisher/input session; the tool rejects
stale health, mismatched revisions, unapproved mappings and invalid PQ. It never
installs the files. The operator reviews and installs that presentation folder
before loading the intended driver. After a Bridge restart, prepare it again for
the new publisher session. No source edit or build is involved.

`driver_monaka_bridge/input_port` is a driver setting, default 29812. Mock Hosts
provide only an OS-assigned ephemeral port through IVRSettings and load the
ordinary built DLL. No real runtime is loaded. Shared wire/default ports and
normalization contracts are unchanged. The source import receipt is maintained
with this task's evidence.

`stored_hil_rehearsal` verifies stored raw/result hashes, exact World candidate,
production file-authority review/prepare/CAS, single selected approval diff,
concurrent writer rejection, actual Bridge normalization and actual DLL Host
registration/Activate/dynamic/lost/expiry/recovery/Rev3 property. Recorded PQ
timestamps are rebased and LV is a declared zero derivative fixture because the
calibration raw artifact contains PQ only. These are software claims, not new
measurements. The test policy is deliberately marked TEST_ONLY_NEVER_DEPLOY.

Historical scope validators retain their original receipts. The readiness child
first admits exact current sources against a committed successor receipt, then
projects only pinned predecessor bytes for historical constraints. Current
functional/native/process tests always execute current source. Mutating current
sources or merely rehashing a receipt cannot bypass the committed admission.
