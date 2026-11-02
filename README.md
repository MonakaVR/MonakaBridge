# MonakaBridge

Normal startup is `../launchers/Start_MonakaBridge.cmd`, using the latest local build at `Build/runtime`.
Run `python scripts/build_runtime.py` explicitly to build that fixed location, then
`../launchers/Start_MonakaBridge.cmd --check` to list missing binaries, DLL or `config/bridge.json`.
The check only reads files; startup never builds, registers drivers or rewrites configuration.
Normal startup opens Bridge Control and its service-owning tray. Configuration,
inspection and calibration remain GUI actions. The only launcher option is `--check`.
Repository launch aliases and historical artifact/operator launch helpers have been removed.
Historical build/deploy instructions below are developer workflows.

Common Observation router and thin SteamVR output. The active MVIP contract kit preserves message-specific wire 2.0/2.1/2.2 semantics; MBIP provides the fixed pure normalization adapter. Software validation and hardware cutover are separate: **hardware cutover pending**. See [responsibility repair](docs/responsibility-refactor.md).

Bridge receives multiple backend sources on loopback 29810, resolves persistent identities and approved profiles, applies shared calibration and explicit mount transforms once, and supplies the same calibrated sample to MTP 29811 and the common Direct driver 29812. Utility Observation mirror 29813 stays active independently of output policy. Default policy is `steamvr`.

The SteamVR world calibrator uses that read-only 29813 native Observation mirror,
not health JSON or Monaka Direct output. It fits a proper rigid world transform from
three or more explicit non-collinear tracker/reference captures; measure-only is the
default and config mutation requires `--apply`.

## Build and run

Use Windows x64, MSVC C++17, CMake >=3.20, Python 3, .NET Framework 4.8/WPF Developer Pack. `scripts/import_upstream.py` verifies the actual bundled Task1/2/3 ZIPs and manifests, then restores reference-only extraction under `Build/Dependencies/upstream`. It never reconstructs missing upstream artifacts.

```powershell
python scripts/build_runtime.py
# Prepare and review config/bridge.json separately; no configuration is copied by the build.
..\launchers\Start_MonakaBridge.cmd --check
..\launchers\Start_MonakaBridge.cmd
```

The builder acquires the public SDK into this checkout's `Build/Dependencies/OpenVR`
and checks commit `0924064316de3effbcd1acf1e309182a2deb1c05` plus the exact input hashes
in `dependencies/calibration-openvr-sdk.json`. An existing mismatched SDK fails without
replacement or fallback. External build tools are discovered through PATH/environment.
Explicit developer CMake configuration also accepts `-DOPENVR_SDK=PATH`.

Before enabling output, assign a persistent installation `bridge_id`, select the observed source/device, choose a verified profile, specify the exact input map/revision and destination world, and approve that input space. The example contains **no mappings**. `vive-unverified` remains blocked; `vive-hil-v1` and `vive-hil-v2` are separate explicit historical/HIL candidates, not automatic approval of every `vut-native-v1` input. Calibration and mount rotations are xyzw unit Hamilton quaternions; translations are metres. See [production integration](docs/production-integration.md) for health I/O, mapping identity, profile tests and hardware evidence limits.

The GUI supports source observations, logical IDs, profile/space approval status, shared alignment, route selection, legacy configuration import, and Bridge start/stop. Profile permutations and explicit mount/world rotations are advanced configuration fields documented in `docs/configuration.md`. It performs no backend pairing/firmware/reset operation.

Build output includes one driver package at `Build/runtime/steamvr/monaka_bridge`. Register this directory with the installed SteamVR `vrpathreg adddriver` only during the planned cutover. No automatic installation or registration is performed. Stop the Bridge with `monaka_bridge_service.exe --stop`.

## Verification and handoff

Run `python -B ../launchers/tests/test_bridge_pico.py` for the fixed-path, file-only launcher
regression. The normal runtime builder targets the product binaries and does not run
the full suite or a product process. Full native validation/release evidence is a
separate developer workflow described in `docs/release-v2.md`; historical reporters
and seals are not current startup or release evidence.
Frozen historical source-scope tests and baselines remain unchanged; gates referring
to removed launch helpers are historical evidence and are not current launcher validation.

- `docs/direct-regression-crash.md`: original Access Violation, root cause, isolation and rerun evidence.
- `docs/architecture.md`: responsibility boundaries, age mapping and bounded queues.
- `docs/configuration.md`: mappings, profiles, calibration and migration backups.
- `docs/cutover.md`: Task2 Phase B / Task5 gates, serial migration and hardware checklist.
- `dependencies/task2-derived-files.json`: each migrated file's actual origin path, Git blob and SHA256.

Generic core/service can be configured with `-DMB_BUILD_STEAMVR=OFF` on non-Windows systems. A non-Windows build has **NOT RUN** in this workspace. Interactive SteamVR/GUI and physical PICO/VIVE validation have **NOT RUN**; synthetic and standalone PASS results do not authorize hardware path removal.
