# Persistent configuration and migration

`config/bridge.example.json` is a disabled-by-mapping template: default route `steamvr`, persistent ID placeholder, no devices mapped, and VIVE profile unapproved. Copy it once to `config/bridge.json`; never overwrite an existing installation's mapping. The GUI and tools read this common file. Health is in the adjacent `.status.json`; it contains counters and device metadata, not raw packets.

Each profile declares the observed C1 convention, independent signed one-based position and quaternion permutations, approval plus evidence, and whether its angular-space mapping has been verified. PICO compatibility is relative-axis legacy evidence, not proof of the absolute playspace origin. The default placeholder PICO convention must be replaced by the actual observed convention. No VIVE hardware interpretation is implied by synthetic tests.

Each mapping includes exact source/device IDs, logical tracker ID, selected profile, input space/revision, approved-space flag, destination world space/revision, shared world transform and explicit per-device mount transform. Rotation arrays are xyzw unit quaternions; translation arrays are metres. Position Zero and Orientation Zero are diagnostic compatibility functions, never silently persisted into the normal world calibration.

Every route/mapping/profile/calibration edit must increase the unsigned 32-bit `mapping_revision`. Reload rejects an older/equal revision, changes to installation identity, duplicate IDs, invalid/nonfinite transforms and out-of-range values. A config mutation invalidates old route/identity poses before republishing the current sample with its original age. Change the destination world revision when the external meaning of that space changes. On revision exhaustion, coordinate a session restart instead of wrapping.

```powershell
./build/Release/monaka_bridge_config.exe config/bridge.json validate
./build/Release/monaka_bridge_config.exe config/bridge.json policy monaka
./build/Release/monaka_bridge_config.exe config/bridge.json align LOGICAL_ID 0.1 0 0
./build/Release/monaka_bridge_config.exe config/bridge.json rename OLD_ID NEW_ID
./build/Release/monaka_bridge_calibrator.exe --config config/bridge.json --tracker RUNTIME_SERIAL --reference left --measure-only
```

The alignment command updates all mappings sharing the selected source/input-map/revision. The migrated OpenVR calibrator retains reference averaging and measurement from Task2 and uses the same grouping; it rejects a concurrent revision change during measurement. `--clear` additionally requires `--config` and `--tracker` so it cannot clear an unspecified playspace. Full rotation/mount/profile edits are explicit configuration edits followed by validation and revision increment.

Native edits retain the first original `.pre-monaka-bridge.bak` and atomically replace a fully written candidate. GUI edits validate a unique candidate and use `File.Replace` with a unique backup. Concurrent multi-writer edits should be avoided; the GUI/calibrator detect revision changes before their operation, but there is no distributed lock across separate configuration tools. Failed candidates are retained for inspection.

Legacy route migration supports `steamvr-direct` and `monaka-external`; alignment supports Task2 version 1 `xMeters/yMeters/zMeters`. Provide an explicit mapping template and a new destination:

```powershell
./build/Release/monaka_bridge_config.exe config/bridge.json migrate LEGACY_ROUTE LEGACY_ALIGNMENT NEW_CONFIG
```

Both legacy originals are backed up and preserved. Imported world translation is copied into explicit source-space groups, output space approval is cleared for review, and the new namespace/version is `monaka.bridge` / 2. A nonempty mapping is required. The GUI offers the same migration with file selection and an additional backup of the common config. Unknown legacy formats are rejected.
