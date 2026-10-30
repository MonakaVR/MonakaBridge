# Explicit installation runtime owner

The existing managed tray owns its native Bridge service and one on-demand WPF
child. A new opt-in launch accepts `--runtime-ports INGRESS MONAKA DIRECT MIRROR`
and `--runtime-logs ABSOLUTE_DIRECTORY` together. All ports must be explicit,
valid, operator-selected endpoints. Default launch values are unchanged. The
native service still uses its current public codec, configuration and owner
lifecycle implementation. No input producer or physical-device control is added.

Use a separate copied configuration, installation profile and log directory for
offline acceptance. Its health path must equal `CONFIG.status.json`, as written
by the native service. Reserve unique OS-assigned ports in the harness, release
immediately before launch, and treat bind failure as failure (never stop the
other owner). Disabled output prevents downstream output during owner tests.

The installation singleton is keyed by the canonical configuration path. An
independent controller identity persists while the service is stopped. Reopen
validates retained process creation time, executable path and hash before
signalling the installation-specific open event. It focuses the existing WPF
child; closing that child leaves the native service running.

Explicitly launched child windows receive a random versioned owner pipe and
exact owner PID/creation identity. Before sending a command the client verifies
the retained owner and the actual Windows pipe server PID. The server validates
the configuration identity, command and version. It accepts bounded messages
with deadlines. Read-only and standalone windows cannot control a service.
Start/Stop/Restart reports native ready or confirmed process exit. Timeout,
missing owner and malformed/foreign identity are UNCONFIRMED, never success.
These results establish lifecycle state only, not tracking, output approval or
SteamVR visibility. A config save remains distinct from runtime application.

Each controller serializes start, stop and exit. Exit permanently fences future
starts, closes its WPF child (which first closes calibration/awaits transactions
and lifecycle requests), then waits for its native service. Failed confirmation
retains the tray; no name-based termination or replacement occurs. Scoped
controllers do not reject unrelated installations merely because their binary
has the same name; native endpoint ownership and retained child handles decide.
Legacy launch retains its conservative unowned-process guard.

`installation_owner` executes the normal native service and normal GUI owner
client/server classes on real Windows with two synthetic isolated configs.
It checks process/UDP/mapping release, duplicate ownership, stale identities,
foreign targets, read-only rejection and delayed start after exit. No physical
HIL or real configuration migration is established by this test. Native Agent
replacement of the existing managed tray is not part of this change.
