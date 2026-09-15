# Task 4 upstream handoff status

This file records the verified upstream state at Task 4 preparation time. It supplements the normative Task 4 specification; it does not replace upstream artifacts or the fixed C1 contract.

## Task 1 / MonakaProtocol

Verified fixed candidate protocol handoff:

- repository: `MonakaVR/MonakaProtocol`
- source branch: `refactor/monaka-layer-separation`
- source commit: `5f41586b51bd84bb1cea344879b5d6325fc2c47d`
- schema commit: `04f2d6c831c68a65edfbfb66ff8838d1c9d78535`
- artifact: `monaka-protocol-kit-v1.0.zip`
- artifact SHA256: `eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729`
- C1 SHA256: `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3`
- protocol.lock SHA256: `234f0dffc46b808a179ec91da3c185794d7b7c83bc5b7d2bcb31fa73886bcb44`
- contract status: `candidate / master reconciliation pending`

Task 4 must use this exact kit. Do not regenerate or hand-roll the schema/codec from this document.

## Task 2 / PICO Backend Phase A

Verified source state:

- repository: `orzkwsk/PicoMotionTrackerBridge`
- implementation branch: `refactor/monaka-layer-separation`
- Phase A / extraction HEAD: `c638f158516effd7fc6511daf179b9a927a099a6`
- audited Task 2 base ancestor: `4c0321f02b56a8ce420ba40a988ca9c296f05f61`
- fixed Task 1 kit is imported and verified in that branch
- C1 Observation provider is implemented on loopback `127.0.0.1:29810`
- Windows host Debug build and 8 CTest cases were reported PASS
- existing SteamVR build script and Android arm64 build were reported PASS
- new Task 2 hardware validation for the separation work was NOT RUN
- Task 2 Phase B is intentionally NOT STARTED; it waits for Task 4 / Task 5 handoff gates

Task 2 provides a generated Task 4 extraction bundle via `scripts/package_bridge_extraction.py`:

- expected artifact: `pico-bridge-extraction.zip`
- expected external report/manifest: generated outside the ZIP so artifact/source hashes are non-circular
- bundle includes SteamVR output sources, calibration/UI sources, scripts, routing/IPC headers, source-path/blob/SHA256 provenance and migration instructions
- PICO private runtime/ABI/Android implementation is explicitly excluded

The generated ZIP and its external manifest are not stored in the Git tree inspected during Task 4 preparation. Task 4 must receive the generated artifact bytes and manifest before claiming migrated PICO Direct/GUI compatibility or DoD completion.

## Task 3 / VIVE Backend

Verified source state:

- repository: `orzkwsk/ViveUltimateTrackerBridge`
- implementation branch: `refactor/monaka-layer-separation`
- Task 3 HEAD: `eaaee63fcacb76d095ff743944fbff1f1427f015`
- audited ancestor: `dev/vivehub-bypass@db7c6795055fc00054b43a64e3bbd658a3575932`
- fixed Task 1 kit is imported and verified
- `halfToFloat()` subnormal bug was fixed in separate commit `1b2b172d627c90e78550a0af4c91e6a3add83f23`
- VIVE Observation adapter and bounded nonblocking C1 publisher were implemented
- default C1 destination is loopback port `29810`
- full lowercase MAC is the persistent device identity; slot/RF counters remain vendor-local
- unknown battery and derivative semantics remain null
- native convention is declared as `vut-native-v1` and is explicitly NOT hardware-verified as an MTP/OpenVR identity transform
- Windows Release build/tests, fixed-kit fixture checks and C++/JVM cross-language checks were reported PASS
- C1-disabled diagnostic build was reported PASS
- all physical VIVE hardware validation items were reported NOT RUN

Task 3 provides `scripts/package_handoff.py`, which generates:

- expected artifact: `vive-backend-handoff.zip`
- expected external manifest: `vive-backend-handoff.handoff.json`
- package includes fixed kit/metadata/lock, synthetic VIVE binary fixtures, C1 JSON samples, generic consumer sources, config and implementation notes

The generated Task 3 ZIP and external manifest are not stored in the Git tree inspected during Task 4 preparation. Task 4 must receive them before claiming VIVE fixture interoperability or full DoD completion.

## Task 4 dependency gate

Before completing migration/compatibility work, verify the actual supplied artifacts, not just these metadata values:

1. fixed Task 1 protocol kit + external handoff metadata;
2. Task 2 `pico-bridge-extraction.zip` + external manifest/report;
3. Task 3 `vive-backend-handoff.zip` + external manifest/report.

If Task 2 or Task 3 artifact bytes are absent, Task 4 may still implement empty-repo scaffolding that depends only on the fixed protocol contract, synthetic tests and configuration models. It must not claim PICO extraction migration, VIVE fixture compatibility, Direct regression, hardware cutover, or final DoD.

No branch/file deletion, force push, rebase, amend or history rewrite is allowed while resolving the dependency gate.
