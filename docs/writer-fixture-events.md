# B2 writer review fixture Event contract

Test-only correction based on accepted B2 b39216bcc69687ae2c0cce974ba259146c5982d7.
Production local_api.cpp,2s writer grace,cancelAt=deadline-100ms,API and TTL stay byte-identical.
No SDK/devices/GUI/UDP. The original filesystem-wait995/terminate evidence remains a negative control.

The fixture creates11 manual-reset named Events and120 bytes of pagefile-backed
test telemetry before emitting its synthetic seed. Only review-* fixture modes
publish test_only_review_events. This is not a producer/observation/wire model.
Writer callbacks use Event waits/signals and Interlocked/QPC stores, without file I/O.
The parent observer holds the Events/mapping and exact writer/worker query handles.
It records marker files after process exit and validates each write. Hard process
termination cannot erase signaled Events while the parent retains its handles.

Lost-cancel: writer fills its own stdout pipe, restores wait mode, passes the
production stop check and waits on release Event inside writer-before-write.
The existing main-thread writer-stop-requested hook runs after the first cancel;
it releases the writer Event itself. The writer records writer-call-ready and
executes the actual full-pipe WriteFile after that first cancel. The test requires
ordered native points,writer-failed/finished,writer kernel exit0,and no harddeadline.
No parent disk roundtrip is required inside the100ms window.

Harddeadline: never release the writer Event. Require deadline Event,process exit3,
writer kernel exit3,no writer-call-ready,and no released Event. This is a separate
fallback case; an unexercised lost-cancel path cannot be counted as race PASS.

Native worker/process GetThreadTimes/GetProcessTimes separate shutdown from
Popen/seed startup. Startup remains bounded6s,barrier observation6s,parent exit
wait3s,fixture Event wait10s,shutdown acceptance2.8s from actual worker exit.
The original2.8s Popen-to-process-exit acceptance is also retained independently.
These retain the existing bounds; the production2s clock still starts after join.
Exact raw IPC,stage errors/exit81/82,phase timestamps,Event states,thread exits,
ambient error diagnostics and parent-created markers are retained per case.

All accepted B2/B1/runtime pins remain byte-identical. writer-fixture-baseline
pins this B2 test layer. After an explicit cherry-pick onto accepted N0a31f47a4,
writer-fixture-n0a-baseline separately pins the current-tree test integration and
the immutable cherry-picked B2 pin. N0a's two test validators receive only layer/
scratch-copy connections; ProviderOpenOptions/session production source is untouched.
All current bytes are rechecked,with only immutable Git blobs cached.
