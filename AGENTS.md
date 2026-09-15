# MonakaBridge repository instructions

This repository owns the common MonakaVR bridge/router layer between device-specific backends and downstream tracking consumers. It receives fixed C1 `TrackerObservation` / `ObservationDeviceState` messages, manages source/device identity, coordinate profiles, calibration, logical tracker mapping, output policy, and emits normalized MTP / SteamVR Direct output through one common pipeline.

## Codex task entry point

For the current layer-separation work, read `docs/codex/Task4_MonakaBridge.md` and `docs/codex/task4/00_upstream_handoff_status.md` completely before implementation. Treat Task 1's fixed protocol kit as the sole C1 contract owner.

## Codex Cloud branch handling

Codex Cloud may expose its writable checkout as an internal branch named `work`. This is allowed when the checkout ancestry derives from the prepared `refactor/monaka-layer-separation` branch. Do not fail only because the local branch name differs, and do not rewrite history to recreate a branch name inside a managed checkout.

## Repository-wide constraints

- Work only in this repository for Task 4.
- Do not commit to or modify Task 1/2/3/5 repositories from this task.
- Do not redefine C1 wire fields, protocol types, ports, version rules, units, or public API names. Use the fixed Task 1 kit.
- Do not copy PICO runtime/private ABI, VIVE HID/RF/vendor packet parsing, body-role assignment, Fusion/Fallback/IK, or device-specific raw transport into MonakaBridge.
- Do not silently treat unknown coordinate profiles or unverified VIVE native space as identity transforms.
- Preserve provenance, licenses, notices, and source-path/hash metadata when importing Task 2 extraction code.
- Preserve unrelated work and uncommitted changes.
- Do not delete branches or files, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep commits small and reviewable.
- Mark build, mock, compatibility, and hardware validation independently. Never claim hardware cutover when it was not actually performed.
- If required upstream handoff artifacts are missing, scaffold contract-independent modules as allowed by the task, but do not claim migration/compatibility/DoD completion.

If repository state or an upstream artifact conflicts with the task specification, stop at the conflicting dependency boundary, record the discrepancy, and report it rather than inventing a replacement contract or silently adapting behavior.
