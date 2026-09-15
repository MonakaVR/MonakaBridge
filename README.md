# MonakaBridge

Common C1 Observation router and thin SteamVR output. Software validation and hardware cutover are separate: **hardware cutover pending**. The fixed Task1 contract is wire 1.0 candidate, master reconciliation pending.

Bridge receives multiple backend sources on loopback 29810, resolves persistent identities and approved profiles, applies shared calibration and explicit mount transforms once, and supplies the same calibrated sample to MTP 29811 and the common Direct driver 29812. Utility Observation mirror 29813 stays active independently of output policy. Default policy is `steamvr`.

## Build and run

Use Windows x64, MSVC C++17, CMake >=3.20, Python 3, .NET Framework 4.8/WPF Developer Pack. `scripts/import_upstream.py` verifies the actual bundled Task1/2/3 ZIPs and manifests, then restores reference-only extraction under `build/upstream`. It never reconstructs missing upstream artifacts.

```powershell
python scripts/import_upstream.py
git clone --depth 1 --branch v2.15.6 https://github.com/ValveSoftware/openvr.git build/openvr-sdk
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
./scripts/build_gui.ps1
Copy-Item config/bridge.example.json config/bridge.json
./build/Release/monaka_bridge_service.exe config/bridge.json
# Separate terminal, or use -Run on the GUI build script:
./build-gui/monaka_bridge_control.exe .
```

The SDK must be commit `0924064316de3effbcd1acf1e309182a2deb1c05`; do not replace an existing checkout blindly. See `dependencies/openvr.lock.json` for exact input file hashes. Configure accepts `-DOPENVR_SDK=PATH`. This workspace was verified with Visual Studio 18 2026 / MSVC 19.51 / SDK 10.0.26100.0. `scripts/build_windows.py` handles the host's duplicate Path/PATH MSBuild environment issue without changing the system environment.

Before enabling output, assign a persistent installation `bridge_id`, select the observed source/device, choose a verified profile, specify the exact input map/revision and destination world, and approve that input space. The example contains **no mappings**. VIVE `vut-native-v1` stays unapproved; illustrative identity arrays are not hardware evidence. Calibration and mount rotations are xyzw unit Hamilton quaternions; translations are metres.

The GUI supports source observations, logical IDs, profile/space approval status, shared alignment, route selection, legacy configuration import, and Bridge start/stop. Profile permutations and explicit mount/world rotations are advanced configuration fields documented in `docs/configuration.md`. It performs no backend pairing/firmware/reset operation.

Build output includes one driver package at `build/steamvr/monaka_bridge`. Register this directory with the installed SteamVR `vrpathreg adddriver` only during the planned cutover. No automatic installation or registration is performed. Stop the Bridge with `monaka_bridge_service.exe --stop`.

## Verification and handoff

Run `ctest --test-dir build -C Release --output-on-failure`. `scripts/verify_windows.py` additionally records standalone Direct execution, DLL imports, the caught original null-context reproduction/call stack, a separate-process UDP regression, 74 fixed codec fixtures / 44 C++↔JVM directions, and actual CPU/allocation/latency measurements. All evidence is saved under `build/validation` and packaged by `scripts/package_handoff.py`.

- `docs/direct-regression-crash.md`: original Access Violation, root cause, isolation and rerun evidence.
- `docs/architecture.md`: responsibility boundaries, age mapping and bounded queues.
- `docs/configuration.md`: mappings, profiles, calibration and migration backups.
- `docs/cutover.md`: Task2 Phase B / Task5 gates, serial migration and hardware checklist.
- `dependencies/task2-derived-files.json`: each migrated file's actual origin path, Git blob and SHA256.

Generic core/service can be configured with `-DMB_BUILD_STEAMVR=OFF` on non-Windows systems. A non-Windows build has **NOT RUN** in this workspace. Interactive SteamVR/GUI and physical PICO/VIVE validation have **NOT RUN**; synthetic and standalone PASS results do not authorize hardware path removal.
