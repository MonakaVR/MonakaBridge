# Shared configuration transactions

`monaka_bridge_config CONFIG transaction-version` returns
`monaka-config-transaction-v1`. The new Desktop requires this capability before
committing. `CONFIG commit EXPECTED_FILE CANDIDATE_FILE` performs exact-byte CAS;
the expected file is the original loaded bytes, not a reserialization. Candidate
configuration and revision+1 are validated. A conflict requires reload and user
review; the command never silently rebases/retries a stale edit.

All updated writers share `commitConfigDocument`: CLI policy/alignment/rename,
tray (through CLI), WPF mapping/migration (through commit), and static/trajectory
calibrator apply (through snapshot-bound saveConfig). The native typed serializer
retains unknown top-level/profile/mapping/nested members from its loaded document.
Config's sourceDocument/sourcePath are internal I/O metadata, never protocol data.

Ownership uses a canonical per-target `.config-write.lock` file with an exclusive
Windows handle / nonblocking POSIX flock. The file remains; process exit releases
the OS ownership. Never delete a lock by age/PID. Existing `.desktop-policy.lock`
markers are not acquired or removed by updated tools and cannot strand a new save.
Symlink targets and hardlink aliases are rejected to avoid multiple lock names.

Under ownership: compare snapshot, validate, stage complete bytes, back up the real
preimage, recheck, atomic replace. Only one same-snapshot writer succeeds. Failure
before replacement preserves target; backup/staging artifacts are not authoritative.
Orphan staging files are never promoted. Cancellation before native commit prevents
it; interruption racing replacement requires readback. Desktop reports an observed
completed candidate as saved, otherwise reports interrupted/unknown and requires
reload. No automatic rollback/retry overwrites a later valid writer. The caller's
15-second timeout bounds a stuck child; only its owned child is terminated.

## Upgrade and guarantee boundary

1. Keep frozen packages/evidence immutable. Build and verify a separate new tool
   set; use the new config CLI with the new Desktop/WPF/tray/calibrator together.
2. Finish/close old configuration editors before the switch. Back up the actual
   current config and review its revision; do not restore an older backup blindly.
3. Point Desktop at the new CLI, verify transaction-version, reload and review the
   selected target. New Desktop refuses an old CLI even without P1 extensions.
4. Do not use old CLI/tray/calibrator binaries to write that target. Retain them
   only as historical/readonly test tools outside the active tool set.

An unchanged legacy executable can ignore the lock and replace a writable target.
Cooperating updated writers are protected; arbitrary noncooperating tools/manual
edits are not atomically excluded. Enforcing executable-level exclusion requires a
separate authority/access-control design. No wire/config version change, service
stop, HIL-ready claim or old artifact overwrite is part of this update.

Regression includes the preserved old2/8 negative control, actual Desktop/CLI/tray/
WPF scheduling, typed native/calibrator snapshot path, simultaneous writers,
unknown-field preservation, stale snapshots, injected failures and native/GUI hard
exit recovery. Hardware, manual OS UI and full signed installer remain NOT RUN.
