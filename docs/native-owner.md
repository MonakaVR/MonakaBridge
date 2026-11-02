# Native installation owner (local candidate)

The `monaka_bridge_service --native-owner` entry combines the existing Bridge
runtime loop and Win32 tray in one native process. It starts without a GUI. The
existing headless service/managed tray remain explicit legacy diagnostic paths;
this candidate does not install or switch an existing user's launcher.
Duplicate default/`--tray-only` launch only probes the existing owner. Explicit
`--open` requests GUI display/focus; quiet duplicate launch never opens a window.

```
monaka_bridge_service --native-owner ABS_CONFIG ABS_GUI ABS_CONFIG_HELPER INGRESS MONAKA DIRECT MIRROR [--open] [--read-only]
monaka_bridge_service --owner-command ABS_CONFIG Start|Stop|Restart|Open|Close|Exit|Probe
```

Add `--installation ABS_PROFILE` for a version2 calibration profile. Its config,
health, config-helper hash and mirror port must match this native installation.
The profile is hash-pinned across GUI reopen. The existing product profile parser
retains World/Mount calibration selection and helper pins. Selecting/creating a
matching profile in WPF retains the verified owner; a native window refuses a
different installation rather than applying commands to the wrong runtime.

`--language en|ja` selects English/Japanese presentation, defaulting to English.
The native tray also offers English and Japanese selections while WPF is closed;
it refuses to discard an open window's edits or active work to change language.
Reopen forwards the chosen language. Controls, calibration instructions and
profile-dialog labels are translated. Native contract states/JSON and technical
diagnostics remain verbatim under a translated diagnostic heading. Config data,
opaque identities, policy values, numeric parsing and protocol bytes are not
translated or rewritten; the language choice is process-local and not saved.

Use independently reserved endpoints and a copied `policy=disabled` configuration
for offline tests. The runtime uses exactly the same codec, calibration, routing,
health and configuration reload loop as the headless service. No producer process,
device, proprietary API or existing SteamVR instance is accessed by ownership code.

The config-path singleton, process creation identity, executable SHA256, random
local pipe and actual pipe server PID establish the native parent. Startup pins
the GUI/helper binaries. WPF is created suspended, enrolled in a kill-on-close
Job, then resumed. Its native management mode requires a successful parent
handshake before displaying. Explicit legacy `--control-window-only` remains a
diagnostic mode with no runtime controls unless its owner channel is verified.

Start/Stop/Restart report worker ready or confirmed worker exit/socket release,
never tracking/output/HIL success. Open/Close/Exit report request receipt only;
callers must observe the corresponding process/window/endpoint state. Bounded
request/response acknowledgement drains IPC before deferred GUI/Exit dispatch.
GUI Close uses its normal safe-close handler. GUI crash permits later reopen;
native owner death closes its Job and owned descendants. Exit waits for the GUI
and its descendants before stopping the runtime; failure retains ownership and
reports UNCONFIRMED. No same-name process termination is used.

The native loop refreshes a heartbeat and re-registers its tray icon on
TaskbarCreated. A fifteen-second loop stall causes the watchdog to terminate only its
own native process, releasing sockets and its GUI Job. This is a fatal path, not
a graceful exit confirmation. Explorer restart handling does not restart or
kill the runtime. The watchdog deadline exceeds bounded GUI-close/runtime-stop
operations combined (Restart can wait up to ten seconds); acceptance must report any untested failure injection.

The menu and `Status` command read the worker's current synchronized state,
including post-ready failure, rather than a cached startup message. Worker and
IPC threads publish a window message; only the window thread mutates the shell
icon. Read-only policy is enforced by the native external-command boundary as
well as by WPF/menu availability. Internal startup and owned Exit remain allowed.
Native config/profile path aliases (including junctions) are explicitly refused
before starting the runtime, keeping the C++/WPF installation identity identical.
SHA256 pins accept validated upper/lower/mixed hexadecimal case; opaque IDs do
not receive hash-specific normalization.

`native_owner_process` exercises actual Windows processes, actual product WPF
client code and real isolated loopback sockets. It is not physical HIL or visual
UI acceptance. Historical immutable scope receipts stay intact; a new exact
native-successor receipt admits these files. Historical projections affect only
old source-shape assertions. Functional tests compile and execute current code.
