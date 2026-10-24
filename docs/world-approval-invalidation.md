# Shared World approval invalidation (2026-10-19)

Base: responsibility candidate `7e95371b145a516e51dbba3beb8d526b4c91dd9f`.
The source change is limited to Bridge; no live configuration, launcher,
Backend, Core, protocol kit, physical approval or runtime cutover is applied.

## Adopted rule

Every explicit Shared World persistence operation clears `Binding.spaceApproved`
for all members of the exact `(source, inputSpace, inputRevision)` group.
This includes an identical W and clearing an already-identity W. No floating
point tolerance or quaternion-sign equivalence grants an approval exception.
This conservative rule matches B4's existing explicit-save behavior. A later
optimization would need a separate reviewed policy; current HIL users may need
to repeat their explicit output review after an intentional no-op apply.

Only the selected group's W and binding approvals change. Mount, coordinate
profile approval/evidence/axes, source contract, other groups and output policy
remain unchanged. `world_revision` retains the existing convention, while the
successful operation increments `mapping_revision` exactly once. Measure-only
returns the unchanged configuration. Failed validation/overflow/CAS leaves the
persisted document unchanged. SAVED does not imply runtime application.

## Covered writers

- WPF `Apply shared translation` -> native `align`: shared translation plus
  unconditional group approval invalidation, then existing validated native CAS.
- `worldCalibrationCandidate(apply=true)`: shared rigid transform plus the same
  invalidation. Legacy static `WriteAlignment`, `--clear --apply`, trajectory
  apply, and B4 candidate preparation call this function.
- B4 retains its existing group invalidation after candidate creation. Repeating
  `false` is idempotent; it adds no revision or approval transaction.
- Legacy import already invalidates all explicitly imported groups; unchanged.

## Authority boundaries still open

General native `commit` validates structure/CAS/revision, not physical approval
authority. An explicit admin-authored JSON candidate or a direct typed `saveConfig`
caller can still change W while retaining/setting approval. Runtime reconfigure
consumes validated configuration and does not adopt a new physical-criteria policy.
These paths require the versioned R06 authority/data migration; they are not closed
by this patch. `PHYSICAL_CRITERIA_NOT_ADOPTED` remains enforced in B4 production.

Ordinary mapping edits do not expose rigid W, and the checkbox cannot grant new
approval. The physical replacement path remains PR9 Discovery with explicit
contract/profile selection, unapproved new binding and manual restart. This
task does not claim to replace all historical administration compatibility.

## Regression and source receipts

The new native, actual CLI and unshown normal-WPF tests use four approved members,
three unrelated source/space/revision groups, exact extension preservation,
same-W/rotation/translation/clear, measure-only, validation/overflow/CAS and an
in-memory runtime reload. Real OpenVR/HMD/SteamVR execution is NOT RUN. Actual
legacy clear executes before VR_Init; static/trajectory capture is covered at
the common candidate boundary and by the existing software suites.

The additive `dependencies/world-approval-baseline.json` binds the new head's
approved scope to its immutable parent. Older receipts remain unchanged. Actual
native bytes must equal the two narrowly reviewed patches before any historical
source view is offered to predecessor scope tests. Rehashing a removed
invalidation statement cannot authorize it. Build/test evidence for the old head
is retained as history, not reused as PASS for the new source.
The older B1 protected-input and B2 legacy-byte comparisons use this validated
parent view too; their unchanged-field lists and historical receipts remain
intact. Mutation of the actual World implementation is rejected before those
comparisons, including a direct call to the B2 legacy-byte verifier.
