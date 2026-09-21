# Persistent configuration and migration

For continuous 6DoF recording with an estimated device holding offset, see
[trajectory world calibration](trajectory-calibration.md). It is selected by
`--mode trajectory`; the static multi-point mode below remains the default.

`config/bridge.example.json` is a disabled-by-mapping template: default route `steamvr`, persistent ID placeholder and no devices mapped. `vive-unverified` remains unapproved; the separate `vive-hil-v1` and `vive-hil-v2` profiles require explicit selection and input-space approval. No profile is selected automatically. Copy the example once to `config/bridge.json`; never overwrite an existing installation's mapping. Health is in the adjacent `.status.json`; its pose is the native Observation stage, not final SteamVR output. See [production integration](production-integration.md) for asynchronous I/O, identity/rebind rules and evidence limits.

Each profile declares the observed C1 convention, independent signed one-based position and quaternion permutations, approval plus evidence, and whether its angular-space mapping has been verified. PICO compatibility is relative-axis legacy evidence, not proof of the absolute playspace origin. The default placeholder PICO convention must be replaced by the actual observed convention.

The VIVE HIL profile `vive-hil-v1` is based on 2026-09-20 VIVE Ultimate Tracker measurements: physical back translated along native `-X`, physical up along native `+Y`, and physical left along native `-Z`; native translation units matched metres. The resulting rigid coordinate-basis conversion into `rh_y_up_neg_z_forward` is position `[z, y, -x]` and quaternion `[z, y, -x, w]`. Yaw/Pitch/Roll captures were used as rotational consistency checks; tracker mounting orientation remains a separate per-device `mount` transform. Angular-velocity frame semantics remain unverified, so `angular_space_verified` stays false. Existing `vive-unverified` profiles are intentionally not upgraded implicitly.

This paragraph records the earlier HIL report. Original captures and a capture manifest are not present in this tree. The current cleanup validates the profile in software; it does not claim a new hardware run or create capture hashes. Historical Task3 hardware NOT RUN remains unchanged.

The `vive-hil-v2` profile records the separate 2026-09-21 physical HIL. Physical
translations in both directions on X, Y and Z matched canonical world directions,
so position remains `[x,y,z]`. With identity quaternion component mapping, yaw and
pitch were reversed while roll was correct. The signed mapping `[-x,-y,z,w]`
matched physical yaw, pitch and roll directions. Pose propagation was observed
through VIVE Backend → MonakaBridge → MTP v2 → MonakaVR private HIP → Slime IK →
SlimeVR OpenVR Driver → SteamVR virtual tracker. This does not verify angular
velocity frame semantics: VUT Observation did not publish angular velocity, so
`angular_space_verified` is deliberately false. Original capture files and a
hash-bound manifest do not exist. The historical `vive-unverified` and
`vive-hil-v1` entries remain unchanged and selectable; v2 is never auto-selected.

Each mapping includes exact source/device IDs, logical tracker ID, selected profile, input space/revision, approved-space flag, destination world space/revision, shared world transform and explicit per-device mount transform. Rotation arrays are xyzw unit quaternions; translation arrays are metres. Position Zero and Orientation Zero are diagnostic compatibility functions, never silently persisted into the normal world calibration.

Every route/mapping/profile/calibration edit must increase the unsigned 32-bit `mapping_revision`. Reload rejects an older/equal revision, changes to installation identity, duplicate IDs, invalid/nonfinite transforms and out-of-range values. A config mutation invalidates old route/identity poses before republishing the current sample with its original age. Change the destination world revision when the external meaning of that space changes. On revision exhaustion, coordinate a session restart instead of wrapping.

```powershell
./build/Release/monaka_bridge_config.exe config/bridge.json validate
./build/Release/monaka_bridge_config.exe config/bridge.json policy monaka
./build/Release/monaka_bridge_config.exe config/bridge.json align LOGICAL_ID 0.1 0 0
./build/Release/monaka_bridge_config.exe config/bridge.json rename OLD_ID NEW_ID
./build/Release/monaka_bridge_calibrator.exe --config config/bridge.json --tracker waist --source vive-local-1 --device 23:32:03:81:fe:ec --input-space tracker-0-map --input-revision 56 --reference left --measure-only
```

The alignment command updates all mappings sharing the selected source/input-map/revision. The
OpenVR calibrator now obtains the exact native tracker pose from the existing read-only Utility
Observation mirror on `127.0.0.1:29813`; it does not require the Monaka Direct driver and never
uses health JSON as a correctness input. The mirror is before profile/world/mount transformation,
while the left/right controller or HMD reference is read from SteamVR's standing universe.

Calibration requires explicit logical tracker, source, device, input-space and input-revision
selection. It locks the first source session and aborts if session or coordinate-space revision
changes, if the config/profile/mapping changes, or if the mirror stops supplying the exact device.
Unknown addresses are not rebound. The selected profile's signed position mapping and any existing
mount translation are applied to the source points; profile axes, mount rotation/translation and
input approval are never edited.

Capture at least three non-collinear locations. At each location, keep the Altra and SteamVR
reference device rigidly fixed relative to one another, keep their orientation nearly unchanged,
hold the assembly still, and press Enter. Multiple new mirror/reference frames are averaged at
each point. The solver computes one proper right-handed Hamilton-xyzw rigid transform in metres,
rejects insufficient/collinear/reflected geometry, and prints RMS and maximum residuals. With a
fixed nonzero device offset, keeping orientation constant lets that offset be absorbed into the
translation; co-located reference origins or an accurately configured mount offset are preferable.

Measurement is the default and does not mutate configuration. Add `--apply` only after reviewing
the points and residuals. Apply reloads and revalidates the config, updates only `world.rotation`
and `world.translation` for the existing shared source/input-map/revision group, increments
`mapping_revision` exactly once, performs native validation, and uses the existing atomic
replacement plus first-original backup. It does not change `world_revision`, mount/profile axes,
space approval, or restart the tracking service. `--clear` additionally requires the full exact
selection and `--apply` and restores the shared world transform to identity.

`align`/`rename` reject tracker-name shorthand if it occurs in multiple sources. Use an explicit source/device config edit in that case. Calibration no longer selects a Direct runtime serial; it requires the exact logical tracker/source/device/input-space/revision tuple. `--list-trackers` remains only as a legacy Direct-device diagnostic. Same-source logical tracker reassignment to a different device still requires Bridge restart; a saved configuration is not proof of runtime application.

Native edits retain the first original `.pre-monaka-bridge.bak` and atomically replace a fully written candidate. GUI edits validate a unique candidate and use `File.Replace` with a unique backup. Concurrent multi-writer edits should be avoided; the GUI/calibrator detect revision changes before their operation, but there is no distributed lock across separate configuration tools. Failed candidates are retained for inspection. The calibrator never overwrites the example config or discovers a local `bridge.json`; the operator must pass the intended path explicitly.

Legacy route migration supports `steamvr-direct` and `monaka-external`; alignment supports Task2 version 1 `xMeters/yMeters/zMeters`. Provide an explicit mapping template and a new destination:

```powershell
./build/Release/monaka_bridge_config.exe config/bridge.json migrate LEGACY_ROUTE LEGACY_ALIGNMENT NEW_CONFIG
```

Both legacy originals are backed up and preserved. Imported world translation is copied into explicit source-space groups, output space approval is cleared for review, and the new namespace/version is `monaka.bridge` / 2. A nonempty mapping is required. The GUI offers the same migration with file selection and an additional backup of the common config. Unknown legacy formats are rejected.
