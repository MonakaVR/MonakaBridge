# Architecture revision software checkpoint

The active contract is C2/wire v2. Fixed kit source: 572e58cfa20b8b4335207ea5dbcb3f04c587ddff; ZIP SHA256: a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3. Historical v1 artifacts remain unchanged and separately verified.

- MTP preserves publisher_id (Bridge installation), source_id (backend installation), tracker_id and input source/device/session/sequence/evidence. Logical names may recur under different sources. Direct keys and runtime serials preserve the publisher/source namespace.
- Registry and Direct receiver use a 500ms accepted-traffic lease (Registry uses the configured timeout). Competing sessions do not poison the owner or renew its lease; expired leases permit takeover and retire the old UUID. Same-session peer changes reject.
- Absent/lost/disconnected metadata establishes a timestamp watermark. A strictly newer pose recovers without a present message; delayed state cannot remove a newer pose. Duplicate packets retain first admission age.
- Space/profile faults and conflicting duplicates are tracker-local. Other trackers remain available.
- Direct FULL is a valid paired pose, ROTATION_ONLY uses OpenVR Fallback_RotationOnly without position, and NONE has no usable components. Direct performs no cross-tracker fallback.

Validation on Windows/MSVC Release: build PASS; CTest 10/10 PASS (architecture revision, core, edges, fixed artifact integrity, C1/C2 model validation, Direct regression, expected old null-context crash reproduction, and separate-process UDP). Direct regression also ran standalone. UDP verified service restart/lease, source independence, stale/reorder/duplicate, route changes, consumer reopen and modality. Hardware and interactive SteamVR NOT RUN.

The seven v2 VIVE status samples are freshly generated synthetic output of ViveUltimateTrackerBridge aa26a2b (vut_observation_tests --emit), not recast original Task3 artifacts. SHA256: 96e6fe7bacfa673b2923373f0c23e55a2faef75444534332db2dcaa26d56789a. Original v1 fixture remains historical.

Audit: F01/F02/F03/F05 have targeted regressions; F07 consumes fixed codec validation plus a guarded age conversion. F10 source-bound release packaging and F12 configurable SteamVR driver port remain follow-ups; the driver currently binds default 29812. F11 redistribution grant remains unresolved. Old Task4 packaging/verification tools are historical and must be revised before v2 release evidence is generated.

Remaining: full five-repository wire E2E into MonakaVR, release-tool migration, runner orchestration, hardware/interactive SteamVR and cutover. No new legacy removal or hardware acceptance.
