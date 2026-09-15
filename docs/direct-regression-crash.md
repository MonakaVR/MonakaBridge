# Standalone Direct regression Access Violation

The original standalone harness called the extracted `TrackerDevice::RequestOrientationZero()` with no OpenVR driver context. The observed failure was an Access Violation reading address zero. It was a real harness defect, not a successful Direct regression. Earlier progress wording did not distinguish the failed invocation and subsequent edited harness sufficiently.

The defect was reproduced against the **unchanged, hash-verified Task2 TrackerDevice** in `old_context_repro.exe`. This reproduction deliberately clears the driver context and catches Windows SEH locally so it does not display another WER/application-error dialog. It requires exception `0xc0000005`, read operation `0`, address `0x0`; a different outcome fails the reproduction test. `/Od /Zi /DEBUG` and DbgHelp capture the stack from the exception context.

Observed stack:

```text
exception=0xc0000005 operation=0 address=0x0 expected_null_read=true
vr::VRDriverLog
pico_ot::steamvr::DriverLog                  driver_log.hpp:11
pico_ot::steamvr::TrackerDevice::RequestOrientationZero
                                            tracker_device.cpp:185
trigger                                     tests/old_context_repro.cpp
main
```

In the pinned OpenVR header, `COpenVRDriverContext::VRDriverLog()` obtains the logger by calling `VRDriverContext()->GetGenericInterface(...)` (line 4478). The original Task2 logger's apparent null guard, `if (vr::VRDriverLog() == nullptr)`, already dereferences that missing context while evaluating its condition. Standalone tests cannot use that logger without supplying a context. No physical tracker or SteamVR server is needed to reproduce the defect. A screenshot address alone cannot identify which binary invocation produced it; the retained reproduction and rerun records provide the causal evidence.

The repaired `direct_regression.exe` owns an RAII `StandaloneDriverContext` before constructing the legacy device. It supplies only a local `IVRDriverLog`, records log requests/messages, throws on any other runtime-interface or handle request, and clears OpenVR's cached context at teardown. The device remains unactivated. No provider `Init`, tracker `Activate`, `VR_Init`, runtime property service or `VRServerDriverHost` is called. The regression checks that only the mock logger was requested. It compares actual legacy pose, velocity, angular velocity, quaternion, time offset and diagnostic zero behavior against the common Bridge path.

The standalone regression target no longer links `openvr_api.lib`. `dumpbin /dependents` confirmed no `openvr_api.dll` or `vrclient` dependency. The separate-process UDP consumer similarly uses the pure `MtpFeed`/`directPose` conversion; it does not load SteamVR.

After the fix, the required executions are recorded independently by `scripts/verify_windows.py`:

1. `build/Release/direct_regression.exe` standalone, with exit code and binary SHA256.
2. CTest, including the Direct regression and the separately named expected-fault reproduction.
3. Real loopback UDP: standalone Bridge service and separate Direct consumer process, using actual Task2 extraction fixture values and all seven supplied Task3 C1 status samples. Includes restart, partial pose, consumer reopen, route changes, double-bind rejection and identical MTP/Direct calibration.

The caught **expected-fault reproduction** is evidence of the old defect; its successful reproduction must not be confused with successful production behavior. The fixed standalone test, CTest and UDP test must each pass. `build/validation-results.json` starts in RUNNING state and records failures rather than carrying an earlier PASS forward. Full logs and exact binary hashes accompany the Task4 handoff. SteamVR runtime/hardware cutover remains NOT RUN.
