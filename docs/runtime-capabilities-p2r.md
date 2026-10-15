# Phase 2C-P2R runtime capability integration

The runtime links the canonical MBIP P1 source package pinned at
`3ea3194479cfa1a04b0fcbcf04916186efb4f9d3`. Its MVIP kit is pinned at SHA-256
`1eea7defe82f39f06f45723444d83bfcce652c3ea519e1dbe29664b7fa21f26b`
(MVIP delivery `6dfa595fa308fea64880fb5f501073feed9a726a`).
`dependencies/mbip-p1.lock.json` pins the archive and every extracted source file;
CMake fails closed on drift via `scripts/verify_p1_runtime.py`. No Normalizer
logic is copied into Bridge. Legacy v22 codec targets remain for historical
standalone tests only; runtime `monaka_bridge_core` links the pinned P1 library.

The actual service path is:

```text
Observation UDP -> canonical MVIP DecodeEnvelope -> source/device config authorization
 -> existing Registry peer/session/clock/sequence/space admission
 -> canonical P1 input Normalizer -> Registry typed cache
 -> existing calibrate/mapping (unchanged math)
 -> canonical P1 output Normalizer -> stable logical pose -> Fanout -> MTP UDP
 -> accepted Core MtpInbox/MtpObservationBackend/MtpPoseAdapter
```

Output normalization is necessary because historical Bridge creates derivative
capability declarations dynamically. Input normalization alone cannot close this
boundary. Core Fusion and wire version 2.0 remain unchanged.

Each mapping optionally supplies a `runtime_contract`: `id`, `provenance`,
`input_declaration`, `output_declaration`, and policies `input_policy` and
`output_policy`. Both policies default to STRICT. Mappings without a declaration
remain editable/calibratable but cannot receive or emit tracking data. Support
is never inferred from packets, validity, field presence, model or history.
LEGACY_DYNAMIC must be explicitly selected. Every advertised set must be a
subset of its provisioned declaration. Lifetime declarations cannot change by
removing/readding a mapping; coordinated restart/new session is required.

The PICO software profile explicitly declares input position, orientation and
linear_velocity because the accepted PICO Observation adapter's Header assigns
exactly that stable contract. Output additionally declares angular_velocity
because the accepted generic Bridge calibration supports derived angular
velocity; P0/P1 bridge fixtures establish P/Q/LV/AV. These are semantic support
declarations, never raw gyro/accelerometer authenticity claims. The software
fixture configuration in `tests/runtime_normalization_process.py` uses explicit
synthetic identities/transforms. Real source IDs and calibrated spaces require
operator provisioning; copying fixture identity/approval into HIL is prohibited.

Normalizers only change capabilities. Runtime diagnostics expose the last 64
decisions and aggregate counts, policy/profile/identity/session, advertised and
normalized sets, modified flag and reject reason. Full before/after envelopes
exist only in explicit file replay. `--version` records source/build/P1 pins.

```text
monaka_bridge_service --replay CONFIG INPUT.jsonl OUTPUT.jsonl SESSION [--mtp]
```

Default replay exercises production Observation receive/calibrate/publish.
`--mtp` exercises the exact production output normalizer on unchanged original
P0/P1 MTP fixtures; it is not an alternate wire ingress. No sockets, device
clients, global stop events or drivers are created. Nondefault UDP test ports
use an isolated stop-event name.

Historical scope scripts additionally verify the explicit exact-file
`dependencies/p2r-runtime-baseline.json`. They compare historical algorithm bytes
against the exact accepted base, and hash-check every authorized new runtime
change. This is an explicit phase baseline evolution, separate from the unchanged
5S protected inventory; no unexpected path is exempted.

PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED
FINAL PICO REACQUIRE THRESHOLDS: NOT YET VALIDATED
