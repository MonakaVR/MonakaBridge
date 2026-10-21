# Bridge Control foundation (WS01a)

The native WPF GUI and tray select the same explicit installation, config,
health file and native config helper. This is the Bridge-owned control-plane
foundation. The calibration workflow (WS01b) and normal-menu Both change
(WS01c) remain separate. This delivery does not claim full GUI parity.

## Select an installation

Run `monaka_bridge_control.exe --installation ABSOLUTE_PROFILE_PATH`.
Add `--control-window-only` to open WPF without the tray. The WPF **Open
installation profile** action selects another profile without persisting it;
it prompts before discarding an existing form. The tray retains its original
target, so reopen the tray explicitly when switching its installation.

Copy `installation.example.json` to your own profile and set these fields:

```json
{
  "schema": "monaka.bridge.installation.v1",
  "name": "Bridge installation",
  "config": "config/bridge.json",
  "health": "config/bridge.json.status.json",
  "config_helper": "monaka_bridge_config.exe",
  "helper_sha256": "REPLACE_WITH_PACKAGED_HELPER_SHA256",
  "read_only": true
}
```

Profile paths resolve relative to that profile. No config or profile is created
at startup. The package intentionally contains no live config or Bridge service.
An unavailable config is displayed as unavailable; use an existing reviewed
configuration and the helper built from the same source/toolset. No repository
layout, Python wrapper or development workspace path is needed by the product.

Explicit absolute `--config`, `--health`, `--config-helper`, optional
`--helper-sha256` and `--installation-name` are also supported. Explicit path
selection defaults to read-only. `--allow-write` is an explicit opt-in;
`--read-only` always disables editing and conflicts with `--allow-write`.
Profile `read_only` defaults to true. An old positional repository root remains
supported with its existing config/helper paths and editing behavior.
Unknown, duplicate, relative explicit paths and ambiguous combinations fail.
The tray passes exact target paths and access mode to its WPF child.

The UI displays selected targets, read-only/access errors, missing/hash-mismatched
helpers and transaction generation incompatibility. Availability queries are
bounded read-only helper calls. Filesystem access checked without startup writes
cannot prove later directory access; native commit remains authoritative.

## Save and observe

All GUI/tray writes use the existing native config transaction. Mapping edits
retain the original exact bytes. Policy, shared translation and legacy import
transform a private temporary copy using the existing CLI, then commit against
that original snapshot. No calibration solver or runtime mapping code changes.
The helper must report `monaka-config-transaction-v1`. If the candidate contains
`runtime_contract`, an invalid declaration probe on another temporary copy must
be rejected before save, preventing an older helper silently ignoring that
extension. Optional package SHA256 pinning also detects a wrong helper.

Native ownership, exact-byte CAS, revision +1, unknown-member preservation,
backup and atomic replacement remain those of [config-write-transaction.md](config-write-transaction.md).
Concurrent cooperating writers have one winner. Reload and review on conflict;
there is no silent retry, rebase or rollback. Arbitrary old noncooperating tools
still fall outside that transaction guarantee.

Saving runs outside the UI thread. Editing and target switches are disabled
during a save. Cancel is checked before native commit; once commit starts,
its actual result decides the outcome. A helper timeout terminates only the
newly spawned helper child and reports **outcome unconfirmed**, including its
PID. A cancelled, rejected or uncertain save requires explicit reload/review.
Closing the window requests cancellation without stopping a runtime service.

**SAVED** means native commit succeeded. It does not establish runtime
application, Core/IK processing or SteamVR output. Config and diagnostic mapping
revisions are shown separately. Matching health publisher and a current snapshot
permit diagnostic comparison only. Missing/malformed, stale, future or mismatched
health cannot prove application; stale poses remain historical. A matching
publisher is not proof of exact process ownership.

**Start, Stop and Restart are unsupported and disabled for every target**, including
legacy-root mode. Handlers reject these operations. There is no global stop
event or image-name termination. An owned lifecycle protocol requires separate
design and review.

## Build, test and package

Build the current native `monaka_bridge_config` target with the repository's
normal CMake toolchain, then:

```powershell
scripts/build_gui.ps1 -OutputDirectory BUILD_GUI -Force
tests/control_foundation_tests.ps1 -RepoRoot REPO -TestDirectory TEST_TEMP -ConfigExe CONFIG_EXE
tests/ui_production_tests.ps1 -RepoRoot REPO -TestDirectory OTHER_TEST_TEMP -ConfigExe CONFIG_EXE
scripts/package_control.ps1 -ConfigExe CONFIG_EXE -OutputDirectory NEW_EMPTY_PACKAGE_PATH
python -B tests/control_packaged_smoke.py PACKAGE_PATH TEST_TEMP
python -B tests/control_foundation_scope_tests.py
```

The package includes WPF/tray, the native helper, example installation profile,
this guide and file/source SHA256 manifest. Packaging refuses an existing output
directory. Tests use synthetic files, hidden WPF controls and owned fixture
processes; they never start the Bridge service, use devices or bind practical UDP.
Manual visual OS acceptance, installer/signing and hardware validation remain
separate gates. The scoped baseline pins current control files and exact
predecessors; historical capability/config/P2R pins and algorithm bytes remain
unchanged. Current-source drift and old-pin mutations must fail.

Health policy must be present as one of the four canonical string values.
Missing, null, wrong-type or unknown values are unavailable/historical in both
WPF and tray; the entire tray presentation has an exception boundary. Regression
drives actual WinForms callbacks, and packaged smoke covers the real tray parent
and WPF child on a private hidden desktop as well as window-only launches.

## Migration and rollback

Keep existing toolsets and frozen HIL evidence. Close old configuration editors,
back up the current selected config, record its revision and review the new
installation profile in read-only mode first. Enable writes only deliberately
with the matching native helper. Do not mix old writable editors with the new
transaction clients. Selecting this toolset does not deploy/restart a runtime.

Rollback the GUI by selecting the prior toolset; inspect its write-contract
compatibility before enabling edits. Never restore an older config automatically
or overwrite a later valid writer. Recover a config only through a separately
reviewed backup/preimage and current snapshot transaction. No installation,
SteamVR registration, live config, service or HIL-ready state is changed by this
software-only delivery.
