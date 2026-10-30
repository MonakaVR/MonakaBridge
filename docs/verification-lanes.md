# Verification lanes — MonakaBridge

## Product purpose
Bridge maps independent public input observations to source-specific coordinate transforms and output routes. MonakaVR extends SlimeVR with 6DoF constraints; neither component is a physical-device quality certification system. See MonakaVR/MonakaBridge#17 for removal of space-revision and physical-approval gates from ordinary use.

## Commands
- `powershell -File scripts/test_revision.ps1 -Mode Quick`: build and selected tests covering coordinate transforms, static/trajectory world calibration, mappings, generic routing, invalid/rotation-only observations, runtime capability declaration, UDP and config transaction.
- `powershell -File scripts/test_revision.ps1 -Mode Full`: SteamVR-capable software build and registered product tests, excluding explicitly named historical byte/scope checks, dormant research components, and old mandatory output-approval test families.
- `powershell -File scripts/test_revision.ps1 -Mode Audit`: the historical all-CTest run including source-scope, dormant-research, physical-approval and release-tooling verification. Run when reviewing retained historical data, compatibility constraints, or release evidence.

The normal CMake configure still checks pinned third-party build inputs. That is a *build dependency*, not proof that a specific tracker maps correctly. Quick/Full do not conduct genuine SteamVR or HIL tests. The old runtime approval policy remains active until the separate production implementation is changed; filtering a historical test does not fix its behavior.

## Invariants
Preserve physical/logical identity, finite values, capability honesty, timestamps and duplicate/reorder prevention, atomic config writes, true source/space convention mapping, and output feedback exclusion. Do not promote a historical source hash or operator-attested calibration quality into a runtime authorization requirement. Existing exact-pinning audit scripts may remain as explicit audits, but should not block unrelated functional development.

When implementing issue #17, replace tests expecting mandatory authorization/rejection with positive tests for retained World/Mount calibration, reuse across restarts, normal 6DoF output, loss/recovery, and user-requested recalibration. Tests whose assertions still encode the old runtime policy cannot be used as acceptance proof for the revised policy. Report device and SteamVR experiments separately as `NOT RUN` until performed.
