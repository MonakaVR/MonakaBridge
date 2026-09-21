# Bridge production integration cleanup

Base: `fix/gui-observation-selection-feedback` at
`57ae12942c38504b4963c0a2731966d42efcf8d2`. The existing 14 commits are retained;
cleanup is forward-only on `cleanup/bridge-production-integration`. No merge into
`refactor/monaka-layer-separation` or VIVE repository change is part of this work.

## Software behavior and validation

Health is diagnostic, not an input to tracking correctness. The tracking owner
captures a value snapshot every 500 ms. `HealthWriter::publish` uses `try_lock`
and a single latest-wins pending slot. Contention may drop a snapshot. One worker
owns JSON serialization, temporary-file write/flush, atomic replacement and the
bounded Windows retry (20 attempts, 10 ms between attempts). Neither file I/O,
health error logging nor retry sleeps execute in the receive/output loop. The
existing 1 ms polling cadence and separate config reload path are unchanged.

The worker never reads mutable Bridge registry/config/cache objects. Failed
writes/replacements cannot end the tracking loop, and failure/drop counters are
included in subsequent health snapshots. Shutdown discards pending diagnostics,
interrupts a retry wait and joins the worker. It does not detach a thread holding
service-owned data. An in-progress operating-system file operation must return
before the join completes; this is not a hard real-time disk/shutdown guarantee.

GUI health reads use one background job with no retry. The UI thread only requests
a job and applies its result through the dispatcher; timer ticks do not queue more
reads while one is outstanding. Read/parse failure retains the preceding values
and explicitly marks them unavailable/historical. Closing the window drops late
completions without joining a stalled file read on the UI thread. Manual config
editing/validation remains a separate user action, not part of the health poller.

Health `position` / `orientation_xyzw` values are **native Observation** samples,
before profile, world and mount transforms. They are not canonical/calibrated MTP,
not solver results, and not confirmation of a submitted SteamVR pose. The health
and GUI expose `pose_stage`, coordinate-space id/convention/revision, freshness,
tracking state and separate position/orientation validity. Sample validity can
remain true after the sample becomes stale. UI freshness also accounts for the
snapshot's creation time and configured pose timeout; delayed disk replacement
cannot make an old diagnostic sample look new. Wall time here is only a diagnostic
display clock; production MTP age/session logic continues using monotonic time.

Required software regressions:

| Test | Scope |
|---|---|
| `health_async` | Blocked/failing write and replacement, actual retry wait, continued Bridge admission/MTP fan-out, latest-only pending data, interruptible shutdown, complete atomic snapshot, native pose/stale validity |
| `gui_production` | Build actual WPF assembly, test its background health reader, one-job bound, failure/close handling, policy/stage/space/component presentation, transactional mapping candidates and real native config validation |
| `mapping_contract` | Source/publisher-scoped output identity, collisions, exact runtime serial selection, same-source device rebind restart gate, source-change invalidation |
| `coordinate_profiles` | Actual example config load/selection, +X/+Y/+Z translations, X/Y/Z/mixed rotation action against independent Rodrigues reference, q/-q equivalence, identity quaternion, unselected/other profile isolation |

Quick and Full run these through CTest. The release-v2 gate requires all four and
packages the freshly built/tested GUI assembly alongside the native outputs. Final
source-bound receipts, archive hashes and command logs are generated under ignored
`build/release-v2` and `dist/release-v2`; prior-HEAD PASS artifacts are not reused.
Tests do not open a GUI window, HID device or SteamVR runtime.

## Mapping and identity

Input binding key: `(source_id, device_id)` within this Bridge installation.
Output identity: `(publisher_id, source_id, tracker_id)`, where `publisher_id` is
the config's persistent `bridge_id`. Body assignment is not part of either key.
Session, coordinate revision and modality are also not persistent identity.

- With a mapping selected, Save explicitly edits/rebinds that entry, preserving
  its world/mount settings. It does not append a duplicate.
- Clearing selection clears the edit target. Save then updates an exact existing
  source/device binding or creates a new one. It does not search by tracker name.
- Same tracker names in different sources or publishers are distinct identities.
  Duplicate source/device or publisher/source/tracker mappings fail validation.
- A source change changes output identity even when the tracker name is retained.
- Editing input source/device/map/revision clears the GUI space approval; approval
  must be explicit again. Selecting a candidate profile does not approve a map.
- The GUI builds an isolated candidate; failed validation/replacement does not
  mutate its loaded mapping/revision. Saved candidates use native validation and
  atomic replacement with backup. Multi-writer config locking is still absent.

Existing runtime lifetime protection is retained: moving the same source/tracker
to another device requires Bridge restart. Saving a valid config is not evidence
that the running service accepted it; check health's applied mapping revision.
Changing source creates a distinct logical identity and invalidates the old output.
No automatic reconnect/rebind or body assignment is implemented.

The legacy `align TRACKER ...` and `rename TRACKER ...` shorthand now reject an
ambiguous name across sources without changing config. Use an explicit config edit
for that case. The OpenVR calibrator resolves the same publisher/source/tracker
derived serial used by the Direct driver, rather than the pre-v2 publisher-only
serial. Its selection is software-tested; actual OpenVR measurement is NOT RUN.

## HIL observations versus evidence available here

The pre-cleanup record in `docs/configuration.md` (commit `8aa2998`) reports these
2026-09-20 VIVE observations: physical back follows native -X, up follows +Y, left
follows -Z, and translation units match metres; yaw/pitch/roll were used for
rotational consistency checks. Those observations motivated the existing
`vive-hil-v1` position `[z,y,-x]` and quaternion `[z,y,-x,w]` permutations.

The tree contains the profile, narrative and synthetic tests, **not the original
HIL capture files or a hash-bound capture manifest**. No capture hash/reference is
invented here. This cleanup tests mathematical consistency and actual config
selection; it does not repeat or independently certify those physical measurements.

The `vive-hil-v1` profile keeps its existing approval/evidence fields as an explicit
production candidate. It is never automatically selected for `vut-native-v1`.
`vive-unverified` remains unapproved; other approved profiles are not replaced.
The example still has no mappings. Profile approval, exact input space/revision
approval, and physical world/mount calibration remain independent decisions.

The separate `vive-hil-v2` profile formalizes the 2026-09-21 physical HIL without
rewriting either historical profile. Physical +/-X, +/-Y and +/-Z translations
matched canonical world directions. Identity quaternion component mapping showed
reversed yaw and pitch with correct roll; `[-x,-y,z,w]` matched all three physical
rotation directions. The observed end-to-end path was VIVE Backend → MonakaBridge
→ MTP v2 → MonakaVR private HIP → Slime IK → SlimeVR OpenVR Driver → SteamVR
virtual tracker. `angular_space_verified` remains false because angular-velocity
frame semantics were not tested and VUT Observation did not publish angular
velocity. There is no original capture file or hash-bound manifest. The profile
still requires explicit mapping selection and exact input-space/revision approval.

Unverified hardware semantics include angular-velocity frame (`angular_space_verified`
remains false), map sharing/reset continuity, loss-orientation usability, multiple
tracker behavior and full physical end-to-end equivalence. No synthetic IMU stream,
fallback policy, raw VIVE protocol logic or wire-v2 schema change is introduced.

## Reconnect integration gates

RF reconnect success is not a promise of Bridge-to-MTP recovery. Check separately:

1. The six-byte RF address is not a persistent hardware ID; an address change can
   invalidate the exact device mapping. Do not silently bind it to another tracker.
2. Backend source/device and Bridge publisher/source/tracker identity must still
   match the intended mapping. IP/port, slot and body role cannot substitute for it.
3. A backend session change is subject to lease/retired-session admission rules;
   it does not approve coordinate continuity.
4. The incoming coordinate-space id/convention/revision must match the binding's
   explicitly approved input revision. A new revision is not automatically approved.
5. A config rebind may require Bridge restart as described above. Verify the applied
   mapping revision and output identity before expecting downstream recovery.

Current-cleanup hardware/HIL: **NOT RUN**. SteamVR interactive: **NOT RUN**.
Interactive GUI, physical VIVE/PICO coexistence, actual RF recovery, map continuity,
world/mount calibration and Direct-versus-MTP physical comparison remain integration
gates. Historical Task3 NOT RUN records remain historical records; the later HIL
narrative does not retroactively change them or certify hardware cutover.
