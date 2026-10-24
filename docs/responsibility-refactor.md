# Responsibility boundary repair (2026-10-18)

Base: PR9 `a03557d242c7d0588b3e3012d69a6de737e79715`, including B4.
This branch does not merge the parallel PRs again and does not import the separate
uncommitted Japanese/H1-R1 preview implementation.

## Implementation order and ownership

1. Preserve PR9 Discovery and its unknown-input rejection, exact source contract,
   profile/group checks, offline replacement, identity Mount, false approval,
   exact-byte CAS, backup and manual restart requirement.
2. Permit opening configuration/Discovery after an operator Profile's selected
   mapping changes. Keep executable/DLL/package hashes and config schema checks.
   `scripts/operator_gui_launcher.py` is the reviewed local H1-R1 adapter with this
   narrowly scoped entry repair. The selected mapping mismatch becomes a check
   warning; native calibration Profile validation continues to reject stale
   measurement/save/approval targets. The adapter does not deploy PR9 or start a
   service. Its H1-R1 manifest remains explicit, rather than claiming PR9 provenance.
3. Offer steamvr/monaka/disabled in normal WPF/tray. Preserve and display a saved
   legacy both value until an operator explicitly selects one destination.
4. Treat the mapping checkbox as existing approval status. Ordinary mapping edits
   cannot grant approval or carry approval to another physical/coordinate identity.
   Unchanged saved approvals survive. Separate B4 review still requires the native
   authority and retains PHYSICAL_CRITERIA_NOT_ADOPTED.
5. Display MEASURED (NOT SAVED), PREPARED (NOT SAVED), actual native SAVED outcome,
   or UNKNOWN. Saving never establishes runtime application or downstream output.
6. Verify source, headless native/WPF regressions and packaged identities before
   any GUI/product deployment. Actual OpenVR execution and physical HIL are separate.

## Approval migration limit

The legacy explicit native administration CLI/config compatibility remains. This
change closes the ordinary Mapping Editor grant route; it does not invent a
physical approval policy or claim that every historical native writer is subject
to a new authority. A versioned native migration must separately specify existing
data, legacy admin authority, and production physical criteria before enforcement
can be unified across all writers. Existing users are not blanket-invalidated.

## Preserved gates and evidence

R08/R09/R12/R18/R20/R21/R22/R26 retain their declared backend/identity/design
limits. Do not add repeated private-ABI enumeration, RF stable-ID assumptions,
auto-approval, guessed capability declarations, a second codec, or an unsolicited
DLL loader. Tests use isolated fixtures, not the user's live configuration.

`dependencies/responsibility-baseline.json` is an additive child of PR9. All 16
earlier baseline receipts and protected native/contract/solver files remain
unchanged; old tests receive their historical views only after actual source
validation. The additional mutation suite rejects drift and a rehashed approval
bypass. Old test receipts do not constitute PASS for this new head.
