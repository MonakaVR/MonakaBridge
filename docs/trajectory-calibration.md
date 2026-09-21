# Continuous trajectory world calibration

`--mode trajectory` is an explicit alternative to the existing default
`--mode static`. Static multi-point capture and its command/output compatibility
remain available for development and HIL. Neither mode runs automatically, changes
profile axes, changes input approval, rewrites device identity, or restarts Bridge.

## Model and implementation

The calibrator uses `R(t) = W V(t) E`. `V` is the full Observation pose after the
explicitly approved profile, **before existing world and mount transforms**. `R`
is the controller/HMD device-to-Standing pose. `W` is the shared source-side world
transform. `E` maps reference-device coordinates into tracker coordinates in this
composition; it describes the rigid holding offset, not a new output mount.
Changing how the pair is held should change E, not W. No transform is applied to
the tracking-system-wide SlimeVR output or to other backend sources.

For relative motions `A = inverse(V_i) V_j`, `B = inverse(R_i) R_j`, solve
`A E = E B`. Rotation uses `log(R_A) = R_E log(R_B)` and the existing Horn solver
on zero-centred +/- rotation vectors. Translation solves
`(R_A-I) t_E = R_E t_B-t_A` with conditioning checks. This is a separable hand-eye
method; see the [OpenCV hand-eye calibration documentation](https://docs.opencv.org/doc/doxygen/html/d4/d93/group__calib.html)
for the AX=XB formulation and the need for nonparallel rotation motions. No OpenCV
or other new library/code is imported.

Relative-motion outliers are trimmed, and `W_i = R_i inverse(E) inverse(V_i)` is
estimated with a medoid seed and robust quaternion/translation averaging. Whole
pose residuals select inliers for repeated hand-eye/world estimation. Both inlier
and **all-pair** position/orientation RMS/max are reported; outliers cannot be
hidden by only printing trimmed residuals. There is no Euler-angle averaging,
pose-difference shortcut, static fallback, or identity fallback on failure.

Joint nonlinear refinement is not implemented in this first version.
`trajectoryResiduals(pairs,W,E)` is the objective-evaluation boundary for future
refinement. Recorded source/reference streams remain separate through the
recorder API so future timing refinement need not change wire v2.

## Timing and validity limits

VIVE's supplied backend timestamps identify backend receive time relative to its
session epoch, not a synchronized optical exposure. Mirror packets retain that
epoch and account for Bridge queue time in `sent_at_ns`. The calibrator fixes a
sample's local time once as `arrival_monotonic_ns - (sent_at_ns-timestamp_ns)`.
Duplicate/old sequence packets cannot update it. OpenVR Standing is queried at
prediction zero; the midpoint of the local query call timestamps the reference.
Nearest timestamp pairing does not assume equal rates, does not interpolate over
tracking loss, and rejects pairs beyond `maxPairingDelta` (default 20 ms).

**This does not synchronize clocks or remove unknown device/network/USB delay.**
The 20 ms bound is on the local correspondence estimate, not a guarantee of
physical simultaneity. Slow query calls invalidate reference samples, and a poll
stall exceeding 50 ms aborts to avoid treating a local socket backlog as current.
Packet age over 50 ms, invalid/lost/non-full poses, and invalid reference poses
are excluded. Timing accuracy and effective lag require hardware measurement;
time-offset optimization is not implemented.

The recording freezes publisher (from config), source/device/tracker, profile,
input space/convention/revision, mapping revision, source session/clock, and
reference serial. Native Observation has no Bridge publisher/mapping revision
field; those are checked against the selected local config, not inferred from
UDP. Unrelated sources/devices on the multi-source mirror are ignored. Loss of
the selected input cannot substitute another device. Changes to a selected
identity, session, clock, coordinate space/revision, config/profile/mapping, or
reference device abort the run. Observed Standing-universe reset events abort it.
After a fatal recorder identity error, further samples/solve remain rejected.

## Bounds and software quality defaults

All solver/quality acceptance settings are in `TrajectoryOptions` in
`include/monaka_bridge/trajectory_calibration.hpp`:

| Check | Initial default |
|---|---|
| Recording | 10 seconds, CLI range 5..15 |
| Temporal span / paired sample count | >=4.5 seconds / >=80 |
| Valid timestamp-pair coverage in both streams | >=70% |
| Translation extent (5th..95th percentiles) | X/Y >=0.15 m, Z >=0.10 m |
| Accumulated rotation (100 ms increments) | >=2 radians |
| Rotation-axis covariance diversity | normalized determinant >=0.01 |
| Relative motions | 0.10..2.8 radians; <=64 motions |
| Recording / solver size | <=4096 samples per stream / <=240 fitting pairs |
| Inlier threshold | position <=0.05 m, orientation <=0.15 rad |
| Inlier ratio | >=80%; quality must also pass on inliers |
| Inlier RMS gate | position <=0.025 m, orientation <=0.08 rad |

These are conservative **software starting values**, not measured hardware
tolerances. Many samples, a planar figure-eight, lateral motion alone, yaw alone,
or time elapsed alone cannot pass. `quality()` provides progress (elapsed span,
paired count, XYZ coverage, rotation/diversity); the CLI prints this once a second.
Failure exits nonzero with a reason and leaves configuration unchanged.

## Measure, inspect, apply

Use a newly built calibrator explicitly; do not replace a currently running HIL
binary. `--config` must name the intended existing installation config. Check the
source/device/tracker and exact approved input revision before recording. The
configured destination world must mean SteamVR Standing. Shared input-map group
members must select the same profile and destination world/revision; otherwise
trajectory calibration fails closed. Existing static group policy is unchanged.

Example (replace every selection placeholder with the actual mapping):

```powershell
$calibrator = '<tested-build>/Release/monaka_bridge_calibrator.exe'
& $calibrator --config '<installation>/config/bridge.json' `
  --tracker '<logical-id>' --source '<source-id>' --device '<device-id>' `
  --input-space '<map-id>' --input-revision <revision> --reference left `
  --mode trajectory --duration-seconds 10 --max-pairing-ms 20 --measure-only
```

1. Start the VIVE tracker/backend and Bridge with the approved profile and exact
   input-space approval. Start SteamVR with the selected controller or HMD.
2. Hold/fix tracker and reference rigidly together. A fixture is preferable to
   hand pressure that can slip. Buttons are not required.
3. Press Enter; make a broad left/right figure-eight, add up/down and forward/back
   motion, and rotate through yaw, pitch and roll. Avoid sudden movements.
4. Inspect W, estimated E, identity/revisions, paired/inlier counts, quality,
   duration, and **both** all-pair and inlier residuals. A software PASS is not a
   physical validation result.
5. Repeat at least twice, then change the rigid holding offset and measure again.
   W should remain consistent while E changes. Compare actual controller/tracker
   motion in Standing and the static reference method; residual distributions
   differ, so a smaller RMS alone is not a hardware PASS.
6. When satisfied, explicitly run with `--apply` instead of `--measure-only`.
   **This records and solves a new trajectory**; it does not apply the earlier
   printed result. On successful quality/residual gates it rechecks config and
   uses the existing validated atomic save/backup to update only shared
   source/input-map world transforms and increment mapping_revision once.
   E, mount, profile, approval, and world revision are not rewritten.

Concurrent config writers remain unsupported (no cross-tool transactional lock).
The mirror has one UDP owner; close another calibrator/mirror consumer explicitly
if it owns 29813. This tool never stops it automatically.

## Evidence status

Synthetic tests cover exact W/E recovery, rich translation/rotation, planar /
lateral / yaw / static rejection, Gaussian noise, minority outliers, q/-q,
unequal rates and pairing bounds, invalid/lost samples, identities/session/clock/
revision guards, bounds, and unchanged measure/apply ownership. CLI software tests
exercise actual option parsing and OpenVR matrix conversion without VR_Init.
The existing static `world_calibration` test remains required by the release gate.

Hardware/HIL, physical timing/offset stability, changed holding-offset repeatability,
and SteamVR interactive: **NOT RUN** for this implementation. No capture hashes
or hardware PASS evidence are invented. Existing profile/HIL records remain as-is.
