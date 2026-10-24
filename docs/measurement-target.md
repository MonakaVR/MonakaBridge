# B1 typed configuration freeze

`freezeMeasurementTarget(config, source, device, tracker, inputSpace, inputRevision)`
returns a deep immutable native value. The factory runs `validateConfig`, the
existing explicit target selector, and shared-group compatibility checks.
An approved coordinate profile and evidence are required. `spaceApproved` is
copied unchanged and is not a prerequisite for this measurement-only type.

The frozen key is `(source, inputSpace, inputRevision)`. Its sorted mapping keys,
every member's entire Binding, referenced Profile, and CapabilityProfile presence
and fields are copied. Validation recomputes membership from that frozen key.
It rejects same-revision additions, removal, movement, and field drift. Declaration
order is significant. Finite doubles are compared by IEEE-754 binary64 component
bits, including quaternion sign, signed zero, and finite subnormal differences.
No capability is inferred when a contract is absent.

`TrajectoryRecorder(config, frozenTarget, referenceId, options)` uses the existing
selected WorldCalibrationTarget for unchanged profile-only measurement math. Its
`checkConfig` validates the complete frozen scope and retains sticky failure.
The original WorldCalibrationTarget overload and `validateTrajectoryTarget` stay
strict. Normal Bridge MTP/Direct output still requires output space approval.
No approval-true copy, general bypass flag, wire/config format, or CLI is added.

This is not an operator confirmation token. First-packet session/clock behavior
is preserved; pre-confirm session/clock witnesses, initial pose/state sequence
floors, runtime freshness/peer attribution, GUI/OpenVR/IPC and confirmation are
B2/B3 work. Unknown JSON, formatting, unrelated groups, policy/timeout and whole
document concurrent-writer/CAS guarantees are outside this native scope. B4 static
workflow and product packaging remain incomplete. No physical accuracy/HIL claim.

The independent branch starts at dev
`279a01363f47ad3564b5cd8b9346180c49ee41d3`; WS01a UI changes are not required.
Therefore the exact B1 predecessor layer connects to the existing capability
scope layer on this base. Historical capability/config/P2R pins stay byte intact.
`measurement_target_scope_tests.py` checks current hashes without caching them,
exact changed paths, model headers, solver/legacy byte ranges, historical hashes,
and drift/rollback/scope mutants. Current pins describe this reviewed change,
not a security boundary against an editor who can rewrite source and pins.

Software acceptance vocabulary:
`B1 PASS — TYPED CONFIGURATION FREEZE ONLY / B2-B4 NOT COMPLETE`.
C01, physical output approval, HIL readiness, and complete product parity remain
unresolved by this library-only change.
