# B4 static measurement and independent output review

Base: accepted `aff03d248ae9abbf6b32857705ffcef98aab8514`. R2 sections 10/11.
The existing wire, static/trajectory solvers, coordinate profiles, mount math,
transaction library and resource-free open/missing-declaration contract remain.

## Static software policy 1

Explicit local `open` mode `static` accepts 3..16 points, default 3; pairs/point
1..4096, default 60, points*pairs <=16384. Poll remains 8ms. Existing trajectory
capture stays 10s and its GUI recording allowance stays 20s. Static uses the same
input owner/monitor with repeated bounded guard collection, not legacy CLI latest
averaging or repeated trajectory capture. Point wait is 60s, run five minutes;
point timeout is max(5s,min(60s,pairs*8ms*30)). GUI independently enforces run
five minutes and a conservative 59s wait; per capture request is bounded to 80s
including barriers/solve, always inside the native run/point limits.

Each point uses a new single-use native token. Capture starts after a fresh
barrier; owner pose/state counters advance while waiting and capturing. Source
time is receipt-(sent_at-timestamp), reference time is zero-prediction query
midpoint. Pair against the nearest reference including invalid samples (20ms
maximum), never across a closer invalid sample. Each source and reference sample
is used once; points reject pre-start/repeated timestamps. Total source and
reference samples, including invalid attempts, are bounded to 16384 each.
The unchanged rigid solver rejects insufficient/collinear geometry. Static uses
the existing mount translation and requires valid orientation for nonzero mount;
E and orientation residual are null/not estimated. Residuals and operator point
confirmation do not establish physical stillness or physical accuracy.

Identity/session/clock/peer/space/Standing changes, absence and the existing 1s
liveness watchdog invalidate throughout waiting/points/review. The same immutable
result/candidate path saves W, resets the exact shared group's output approval,
increments revision once, retains other groups and reports runtime unconfirmed.

## Independent output approval

An explicitly opened `purpose=output_approval` session takes a new current config
snapshot and discovers/confirms a fresh owner witness. It never imports historic
draft authority or reuses the completed measurement transaction. `review_output`
shows current saved W/group/frame, per-member eligibility and criteria/evidence
or native refusal reasons. `prepare_output` requires explicit nonempty eligible
member selection, read-write mode and still-valid adopted authority. It creates
a separate exact candidate/diff and review token. `commit_output` revalidates
scope/criteria/frame/artifacts/token, uses existing exact CAS once, increments
revision once and changes only selected false approval flags. W/E/M/profile,
capabilities, identity, unknown JSON and unselected members remain unchanged.
Replay is cached; lost post-entry outcomes are UNKNOWN, with no retry/rollback.

**Production installs no physical criteria authority.** Native preparation/commit
therefore reject with PHYSICAL_CRITERIA_NOT_ADOPTED. No JSON/profile/CLI boolean,
threshold, environment variable or synthetic evidence can enable production
approval. The native authority interface exists for later separately reviewed
physical-policy integration; synthetic implementation is only in test headers
and the separate runtime fixture target. Software success is not physical approval.

New optional local features static_points_policy1/independent_output_review must
match static_supported/output_approval flags. New GUI still accepts old trajectory
helpers; static/output review require the explicit advertised capability and never
fall back to unknown operations. The existing v2 profile remains explicit opt-in.

Normal WPF provides mode/point plan, per-point capture, same-result save, fresh
separate Output Review, evidence/reasons, default-unselected members, native exact
diff and distinct Approve Output action. All added controls have keyboard focus,
labels and automation names; reasons are text. Generic existing mapping approval
is a legacy configuration editor, not proof of this criteria workflow or a
same-user security boundary. No product sender authentication claim is added.

R23-R25 validation uses test-only public-SDK seams and normal WPF controls without
showing windows. Real SDK/SteamVR/practical UDP/hardware/HIL/OS visual remain
NOT_RUN; PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED.
Physical criteria/thresholds, evidence authority and HIL remain separate gates.
