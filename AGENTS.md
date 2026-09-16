# MonakaBridge repository instructions

This repository owns the common MonakaVR bridge/router layer between device-specific backends and downstream tracking consumers. It receives shared TrackerObservation / ObservationDeviceState messages, manages source/device identity, coordinate profiles, calibration, logical tracker mapping, output policy, and emits normalized MTP / SteamVR Direct output through one common pipeline.

## Current coordinated Architecture Revision

The older Task 4 document and its fixed C1/wire-1.0 handoff are historical baseline material for the delivered implementation. For the current coordinated revision, protocol/identity/modality/Main-Fallback semantics are superseded by the Architecture Revision owned in `MonakaVR/MonakaProtocol` (`docs/architecture-revision.md` and `docs/C2.md`). Do not revert current v2 semantics merely to satisfy an older Task 4 paragraph.

Protocol-dependent migration work must use an **actual generated and hash-pinned wire-v2 kit** from a clean committed MonakaProtocol source tree. Until that artifact and manifest are supplied to this workspace, inspect/plan/refactor contract-independent code freely but stop at the protocol dependency boundary. Do not fabricate hashes, infer v2 fields from v1, or hand-roll a substitute codec.

Where the Architecture Revision does not conflict, existing Task 4 calibration, mapping, Direct-output, provenance, and hardware-gate constraints remain applicable.

## Codex task entry point

For Bridge history and non-conflicting Task 4 constraints, read `docs/codex/Task4_MonakaBridge.md` and `docs/codex/task4/00_upstream_handoff_status.md` completely before implementation. Treat their fixed-C1 sections as historical once the coordinated v2 handoff is active.

## Codex Cloud branch handling

Codex Cloud may expose its writable checkout as an internal branch named `work`. This is allowed when the checkout ancestry derives from the prepared `refactor/monaka-layer-separation` branch. Do not fail only because the local branch name differs, and do not rewrite history to recreate a branch name inside a managed checkout.

## Repository-wide constraints

- Work only in this repository unless the active coordinated task explicitly states otherwise.
- Do not redefine shared wire fields, protocol types, ports, version rules, units, or public API names. Use the fixed active MonakaProtocol kit.
- Do not copy PICO runtime/private ABI, VIVE HID/RF/vendor packet parsing, body-role assignment, Main/Fallback selection, Fusion/Fallback/IK, or device-specific raw transport into MonakaBridge.
- Preserve modality and provenance from backend observations; Bridge does not invent rotation-only capability or synthetic IMU streams.
- SteamVR Direct remains one normalized logical tracker path and must not perform MonakaVR cross-source fallback composition.
- Do not silently treat unknown coordinate profiles or unverified VIVE native space as identity transforms.
- Preserve provenance, licenses, notices, and source-path/hash metadata when importing upstream extraction code.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches or files, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep commits small and reviewable.
- Mark build, mock, compatibility, and hardware validation independently. Never claim hardware cutover when it was not actually performed.

If repository state, the active v2 kit, or an upstream artifact conflicts with the current Architecture Revision, stop at the conflicting dependency boundary, record the discrepancy, and report it rather than inventing a replacement contract or silently adapting behavior.
