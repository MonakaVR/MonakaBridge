# Task2 Phase B and Task5 handoff

Status: **implementation complete / hardware cutover pending** after all recorded software checks pass. No Task2/3/5 repository is modified. No old provider/path is deleted, unregistered, or disabled by this implementation.

Software evidence covers the hash-verified fixed Task1 kit, actual Task2 extraction source/blob/file hashes, actual Task3 bundle and seven C1 status samples, old TrackerDevice regression with isolated mock context, separate-process UDP MTP/Direct equality, config/route migration, bounded fan-out, Windows service/driver/calibrator/WPF builds and fixed C++/JVM codec interop. The null-context regression harness crash was reproduced and repaired; see `direct-regression-crash.md` and packaged logs. These checks establish a reviewable software migration baseline, not physical tracking correctness.

Task2 receives `monaka-bridge-handoff.zip`, its external SHA/manifest/report and exact MonakaBridge HEAD. The ZIP contains source/provenance, one common driver package, service/config/calibrator, GUI, input artifacts and software evidence. The external manifest lists hashes without embedding a circular ZIP self-hash. Verify it before planning Phase B. Task2 Phase B still depends on Task5's separate consumer-integration gate and physical compatibility; this Task4 handoff alone does not authorize removing the old path.

Legacy SteamVR serials are PICO serials; new Direct serials are explicitly namespaced using Bridge installation/logical tracker IDs. They are intentionally distinct. Before hardware cutover, export the old serial/role/config list, create a reviewed old-serial → `(source_id, device_id)` → logical tracker → new runtime-serial migration table, and compare/reapply the intended SteamVR assignments. No automatic destructive role migration is performed; body-role meaning remains outside Bridge. Keep the old configuration and binaries available for rollback. Never run old and new ingress owners on the same port; double-bind rejection is tested.

`both` is opt-in. Task5 must consume only MTP on 29811 for the configured Bridge installation IDs. It must exclude all `monaka-direct:` serials and all of its own solver output identities from any OpenVR input discovery. Its output serial namespace must be distinct (for example `monaka-solver:`); that example is a requested integration contract, not a claim that Task5 already implements it. Do not feed Direct output or Task5 output back into Observation ingress. Confirm exclusion and installation identity configuration before enabling `both` outside a synthetic test.

Physical validation, all **NOT RUN** here:

- PICO five-trackers position/orientation/velocity, diagnostic zero, shared calibration and same-point Direct vs MTP comparison.
- VIVE Hub stopped with official dongle input; axes, device-to-space quaternion, one-metre scale, shared map and revision semantics.
- PICO + VIVE coexistence without collision or cross-source reset.
- SteamVR registration, dynamic device lifetime, restart and output route changes.
- Old/new SteamVR serial-role comparison and rollback rehearsal.
- Interactive tray/WPF behavior and actual OpenVR calibrator measurement.

Task3's historical VIVE hardware NOT RUN remains unchanged. The later `vive-hil-v1` candidate records limited axis/scale/rotation observations; it does not establish shared-map/reconnect continuity or complete the above cutover gates. Its source capture files are not in this tree. Profile selection and exact input-map/revision approval remain explicit; see [production integration](production-integration.md). Generic signed permutations must not be extended from unverified vendor assumptions.

Other limits: fixed contract master reconciliation is pending; non-Windows build NOT RUN; transport timestamps do not synchronize clocks or estimate network delay; same-source collision quarantine requires restart; lifetime identity bounds require coordinated Bridge/SteamVR restart after substantial mapping churn. Task2's original source has no root LICENSE grant at its extraction base: preserve bundled file notices and provenance and resolve any redistribution rights separately; no new license is asserted here.
