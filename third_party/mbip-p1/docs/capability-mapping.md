# MVIP capability normalization — Phase 2C-P1 (normative)

MVIP owns vocabulary, wire validation and semantic channel projection. MBIP owns
the deterministic adapter at the Bridge/Core-facing boundary requested for P1.
This is a pure value adapter, not a new DLL ABI, receiver, pairing, calibration,
coordinate mapper or Core policy. The explicit P1 instruction governs this scope;
the repository's broader DLL/input implementation remains a separate phase.

MVIP is consumed through an exact source/content-pinned kit. MBIP MUST NOT carry
an independently maintained capability enum/schema/codec. Translation is owned by
`schema/capability-mapping.json`; generated dispatch tables MUST match it.

| MVIP capability | MVIP usable-sample signal | MBIP representation | Core meaning | Classification |
|---|---|---|---|---|
| position | pose validity.position | position + support/presence/valid facts | use current admitted P only when valid | PASS_THROUGH |
| orientation | pose validity.orientation | orientation + support/presence/valid facts | use current admitted Q only when valid | PASS_THROUGH |
| linear_velocity | non-null derivative/vector + position validity | linear_velocity + facts | m/s in declared frame; no fabricated sample | PASS_THROUGH |
| angular_velocity | non-null derivative/vector + orientation validity | angular_velocity + facts | rad/s; no independent IMU inference | PASS_THROUGH |
| linear_acceleration | non-null gravity-free derivative/vector + position validity | linear_acceleration + facts | m/s², gravity removed | PASS_THROUGH |
| battery_fraction | non-null fraction | battery_fraction + facts | 0..1 metadata; no pose freshness | PASS_THROUGH |
| charging | non-null charging | charging + facts | boolean metadata including false | PASS_THROUGH |
| raw gyro / raw accelerometer requests | no frozen token/field | no channel | no raw IMU claim | UNSUPPORTED |
| unknown or duplicate wire tokens | invalid declaration | no output | fail closed | REJECTED |
| Common World/authority messages | no capabilities | no tracker projection | use separate authority consumer | REJECTED |

`RENAMED` and `DERIVED` are reserved mapping classifications with no current
rows. This adapter derives neither orientation nor angular velocity. It preserves
source derivative frame/evidence and requires existing calibrated MTP for a
normalized Core pose; an Observation projection does not establish calibration.

## Normalization rules

The caller supplies an immutable declaration and exact identity/session binding.
This value is a provisioned/negotiated contract, never generated from a current
sample. STRICT is default: pose/state declarations MUST match as sets. Capability
flapping or a foreign source/publisher/session MUST fail without output. A valid
session restart is represented by a new caller-admitted contract, not automatic
session takeover. This adapter does not select sessions or alter reconnect policy.

LEGACY_DYNAMIC is an explicit migration mode for P0's historical Bridge. It
accepts only a subset of the provisioned structural declaration, then emits that
complete declaration in vocabulary order on both pose and state. Validity,
optional samples, timestamps/sent_at/sequence, publisher/source/input provenance,
device/tracker, coordinate space and mapping revision MUST remain unchanged.
No declaration means no adaptation; no packet-history union or vendor heuristic.
This repairs the old dynamic emission at a bounded adapter without changing the
frozen Core equality rule or touching the old producer/production receiver.

MBIP exposes support separately from effective validity and presence. Consumers
MUST NOT consume an invalid sample merely because support is declared. Producers
MUST NOT drop support on temporary invalidity. State metadata cannot refresh pose.
Tests include deliberately faulty producer/consumer behavior, the exact frozen
Core set-equality decision, and the before/after H-01 packet sequence.

No sender-session/space policy, Issue #6, backend reconnect, Altra ownership,
PICO gate or Core RECOVERY_DWELL/BLEND behavior changes.

`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`
