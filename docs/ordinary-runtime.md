# Ordinary Pose, saved transforms and calibration

Issue #17 (2026-10-30) supersedes historical approval-as-output-permission text.

The normal service accepts a structurally valid public observation with an
explicit static capability contract and a matching, explicitly defined axis
profile. It applies each source's saved World and each device's saved Mount as
`T_out = W_source * T_native * M_tracker`. New uncalibrated mappings use
Identity and show their calibration status. Approval/evidence metadata is
historical information, not output permission.

The discovery view offers the selected device's actual declared static
capabilities as a reviewable contract candidate, including for the first source.
It never derives support from FULL/Q-only/NONE, a name or a vendor. The user
must select a defined matching coordinate profile; unknown axes stay rejected.
Source/device identities are opaque. Updating the same physical mapping's
diagnostic revision retains World and Mount. New-device replacement does not
inherit another physical Mount.

Compatible coordinate ID/revision and transport-session changes clear transient
Pose and derivative histories while retaining saved transforms. Fresh input is
required after reconnect. MVIP and Direct use the same normalized logical state.
Static capabilities stay distinct from current component validity.

Calibration is measurement, review, explicit save, and runtime application.
The GUI does not require a separate Output Approval transaction or load external
physical-evidence files at startup. A same-World save, including quaternion sign
equivalence, performs an exact-snapshot CAS no-op; it preserves the file and
mapping revision. Changed World affects the selected source-space group only.
SAVED still does not claim APPLIED or physical accuracy.

Normal regression includes runtime math, static capabilities, lost/recovered
components, identity/replay, concurrent config writes, native calibration and
GUI-to-native save paths. Legacy source-byte/scope checks remain available with
`-DMB_ENABLE_HISTORICAL_AUDIT=ON`, labeled Audit. Their old fixed-source claims
are not ordinary product requirements and are not reported as passing the new
implementation. Pinned codec/SDK artifacts retain their integrity checks.

Software Mock/Replay results do not establish physical HIL or cutover.
