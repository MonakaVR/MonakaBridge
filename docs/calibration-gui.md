# Normal Bridge trajectory calibration GUI

This U1/U2 layer starts at fixed J0 `9dd8ac0a562f27460358d822ad9b57b7e59329d4` (N1 runtime provider plus accepted control foundation). The normal WPF executable contains the calibration view. There is no Python runtime dependency or alternate calibration GUI/helper.

## Operator flow

The existing v1/root launch remains the default. Select a persisted mapping, then **Create calibration profile (Save As)** to create a separate `monaka.bridge.installation.v2` file. Choose the matching product calibration executable, explicitly enter the configured loopback mirror port, choose left/right/HMD, and declare a single Bridge/no relay. No role, source support, session or capability declaration is inferred. The old operator wrapper uses mirror port 29813; the dialog does not bind or discover it. Save As uses CreateNew and never overwrites a config or existing profile. Saving explicitly selects the new file for this window only; subsequent startup does not upgrade or auto-select it.

The immutable profile freezes target config/health paths, Bridge/source/device/tracker/space/revision, reference role, operator association, two helper paths/hashes and local API/software/GUI contract versions. Effective readonly is sticky across the tray-to-window handoff. The child rereads the selected profile and verifies its exact loaded byte SHA-256; replacement rejects without fallback. Runtime session/clock/Standing/floors are not persisted.

1. **Open session** verifies helper bytes and resource-free version metadata, creates one held Process in one unnamed kill-on-close Job, and opens its local API. Open does not initialize OpenVR or bind a mirror.
2. **Discover target** explicitly starts the N1 Background/Standing/mirror provider. Review the full native witness, including source, session/clock, peer, space/revision, role/serial, generation, Standing witness and explicit input declaration.
3. **Confirm displayed witness**, then **Begin trajectory** within the native confirmation budget. The existing solver remains authoritative.
4. Review the verbatim immutable native result. W is world alignment to save; E is the holding extrinsic and is **not mount**; M is the unchanged existing mount. Group output approval resets, revision increases once, and runtime application/downstream output remain unconfirmed. Static calibration and output approval are outside this flow.
5. **Prepare same result** obtains the exact native candidate. Review its byte count, path, SHA-256 and bounded text preview, then tick the review checkbox and **Save exact candidate**. Commit contains only result ID, native review token and candidate hash. C# never recomputes/rounds W/E/M for storage.
6. **Cancel / close** waits for the owned worker to stop. After save or possible commit, reload/review configuration before **New measurement**. Target change and window shutdown await owned process exit; stale generation callbacks cannot apply to a new target.

Readonly profiles allow measurement but prohibit prepare/commit. Config edits in this window are disabled throughout the owned calibration session. The tray conservatively disables its writes while its own control child lives, including when that window changes from v1 to v2. It does not claim global ownership of unrelated processes or windows. Existing config CAS/lock and service-control unsupported behavior remain.

## Pipe and lifetime contract

Local JSON is bounded, strict UTF-8 and rejects duplicate keys and invalid numeric/types/depth/counts. This is a local control parser, not a second tracking codec/observation model. Stdout lines are at most 17 KiB; stderr is bounded to 256 KiB. Dedicated pipe threads drain independently of WPF. Progress is one replaceable latest value; reliable controls are bounded to seven plus one terminal notification. No per-packet Dispatcher queue is created.

Ordinary requests serialize and stop at 126, leaving cancel and close reserved. Event sequence/previous sequence, request IDs and session/run must match. Cached outcome content remains immutable; native replay annotations (`current_phase`, `eligible`, worker expiry) are checked separately and cannot refresh local deadlines or recreate a prepared candidate. IN_PROGRESS and progress do not renew budgets.

A dedicated monotonic watchdog, independent of WPF and ThreadPool scheduling, enforces version/start 5 s, discover 10 s, confirmation 10 s, record 20 s total software allowance (native capture remains 10 s), solver 10 s, native result review 10 min, candidate review 30 s, worker 20 min, cancel/close grace 2 s and commit 15 s. These are software resource bounds, not new physical tolerances. Expired candidate review requires explicit preparation of the same still-eligible native result. Clock rollback stops the owned worker.

`COMMIT_MAY_HAVE_STARTED` latches before the first commit Write call. Partial/lost write, EOF or deadline after that point is UNKNOWN; the view does not retry, recompute, roll back or relabel it cancelled. Actual same-owner SAVED/SAVED_CONFIG_EVIDENCE_STALE evidence remains visible even during shutdown. Only the held child/Job is terminated, never a process-name/PID/port/service lookup.

## Build and validation boundaries

On Windows, CMake target `monaka_bridge_control` invokes the normal .NET Framework C# compiler with explicit product sources and WPF 4.8 references. `scripts/build_gui.ps1` contains the same source set for environments where that script is permitted. No execution-policy bypass/change or script reinterpretation is used. The new GUI exact-scope leaf preserves all twelve old baseline files as raw fixed J0 bytes and preserves Core, codec/normalizer, solver/math, transaction and legacy CLI product sources.

CTest managed groups cover strict parser, v2 profiles, immutable models, real Windows Job ownership/parent death, real child argument propagation, same C# client to native fake-provider/Session/solver, malformed control pipes, and normal WPF controls instantiated **without showing a window**. No NotifyIcon/hidden desktop/visual workaround is used. Test-only runtime/fault executables are never product helpers.

Actual OpenVR/HMD/SteamVR, practical UDP/HIL and OS visual validation are NOT_RUN. Genuine SDK is compile/link and package provenance only; actual product bootstrap tests must use an isolated sentinel DLL. `PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED` remains.


A finished toolset is built with `scripts/build_control_toolset.py` from an exact clean commit into a fresh build directory. Its receipt records all tracked source hashes before/after, commands, raw logs, complete CTest JUnit and the four product hashes. `scripts/package_calibration_control.py` requires that successful matching receipt and pinned SDK bytes, then includes GUI/config/calibrator/service, genuine DLL/license/header/import library, exact protocol source archive/lock, Git source ZIP and build evidence. `scripts/verify_control_toolset.py` performs static full-inventory/build/product/SDK/fixture verification without launching any executable. It cannot establish real hardware or OS visual behavior.
