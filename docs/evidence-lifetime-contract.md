# Saved physical evidence lifetime

The v2 physical policy and evidence schemas explicitly use `lifetime.mode = semantic`.
Absent or null `not_before_utc_ms` / `not_after_utc_ms` impose no time cap. If present,
they are nonnegative signed 64-bit integer UTC milliseconds. The optional end is
exclusive. No default duration or maximum timestamp substitutes for no expiry.
The v1 schemas retain mandatory finite policy/evidence periods and capture epochs.
Never change a recorded measurement timestamp to migrate a v1 artifact.

Capture freshness, the recording timeout, review lifetime, commit token lifetime,
and fresh source/reference checks are unchanged. Optional evidence expiry is a
separate constraint. Native acquires current witness data; JSON from a GUI or a
saved file cannot assert that a source is currently live.

`FileAuthority::verify` verifies adopted file pins, provenance, native measurements
and policy gates. Its `evaluateEvidence` helper is the single v2 semantic decision
function over that context. The helper alone is not a filesystem hash verifier.
The result reports VALID, INVALID or UNKNOWN, an exact reason and identities.
Only VALID reaches approval. Missing declarations and unavailable/stale current
witnesses remain UNKNOWN. Hash, configuration and semantic changes are INVALID.

Restart does not itself change physical space. Saved capture session/clock IDs
remain immutable provenance. A separately pinned `current_declaration` binds the
operator's SAME/UNKNOWN/CHANGED statement to current sender epochs, exact evidence,
policy and configuration bytes, source/device/space/revision, independent space
generation, attachment relation and reference semantics. GUI/Bridge restart can
reuse the same declaration while those epochs remain current. After a sender or
provider/service restart, prepare and review a declaration for the new epochs.
Never infer SAME from a process restart or reuse a stale epoch declaration.

P0-B owns coordinate revision/generation and continues suppressing valid PQ until
its source space is declared known. The declaration uses that reviewed independent
generation; it is not a private PICO field in MTP/POTB or a new wire extension.
Source, device, revision, generation, World, Mount, profile/capability, reference,
Standing or physical attachment changes invalidate reuse. Config hashes conservatively
bind all configuration semantics. A v2 approval requires evidence for the currently
witnessed target; it does not manufacture fresh witnesses for other devices.

Preparation tools create new review files only and leave adoption disabled. For
legacy captures whose original generation/attachment is not established, the saved
semantic identity is null/UNKNOWN. The operator must establish the original and
current physical relationship from actual evidence or perform a new measurement.
A TEST_ONLY replay declaration proves software behavior, never actual continuity.

Policy v1's physical numbers are unchanged. Native output now also records motion
quality and actual measurement options. Inlier count, ratio, RMS and inlier maxima
remain gates; all-pair maxima are finite recorded diagnostics in v2, without a hard
limit. The existing solver's minimum 80 inliers remains in force. No measurement
or transform is calculated by Python preparation/preflight tools.

Static package preflight checks adoption, artifact bytes and saved metrics. It
does not certify live witness freshness or physical continuity. Native review,
prepare and commit recheck those conditions and recheck pinned declarations;
changing a declaration during a transaction requires a new review.
