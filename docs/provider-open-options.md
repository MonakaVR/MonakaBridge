# N0a: validated open to immutable provider configuration

Contract fixed before implementation. Base B2:
`b39216bcc69687ae2c0cce974ba259146c5982d7`.
This is a native internal configuration handoff, not a new tracking protocol,
capture provider, monitor/thread/factory, SDK, GUI or package feature.

## One owning snapshot

ProviderOpenOptions owns the existing FrozenMeasurementTarget and an optional
selected CapabilityProfile copied from the same validated typed Config. Binding,
Profile, publisher and mapping revision are read from that Frozen target, never
duplicated into an independently editable identity/profile field collection.
The optional declaration remains absent when Config has none; no inference.
The object also owns config path/exact-byte hash, installation path, explicit
mirror address/port, reference role and readonly metadata. No Json/reference to
request storage or borrowed Config data escapes into the provider. No new common
observation model: c1::Envelope and Frame remain unchanged.

Only CalibrationSession constructs the snapshot. It reads/parses the canonical
config document once for open, freezes that exact typed value, derives the
optional contract from it, and builds the owning snapshot. This is not a second
JSON-to-capture model/parser. All existing open validation precedes configure.

## Publication, failure and lifetime

CaptureProvider has a nonvirtual one-attempt configure wrapper. A protected const,
resource-free validation hook may reject configuration. The wrapper publishes its
shared_ptr<const ProviderOpenOptions> only after that hook succeeds. The hook
must not retain borrowed references, install partial targets or acquire capture
resources; diagnostic counters may be mutable in test fixtures only.

Session publishes its config/Frozen target and OPEN only after configure succeeds.
On configure failure: provider configuration stays empty; session enters FAILED;
no OPEN, partial session target, capture resource or draft is published. Failed
request replay returns the cached failure without reconfiguration. A different
open or discover in that terminal session is rejected. There is no reset/retry
operation: use a new worker and a new provider instance. This does not authorize
adding a reset command or rebuilding commit authority from drafts.

Invalid open never calls configure. Same-ID/same-payload replay, same-ID/different
payload rejection, and a second open after success never call configure again.
Provider owns the snapshot after request/config temporaries and Session are
destroyed; a retained shared_ptr owns it after the provider is destroyed too.
No mutable getters/assignment expose identity, profile or declaration drift.

## Preserved B2 boundaries

- Missing declaration still permits open. The existing supported-provider
  discover path retains EXPLICIT_DECLARATION_REQUIRED; N0a does not move it into
  configure/open or infer a declaration.
- Constructor/configure/supported/version/open perform zero VR API calls,
  socket creation/bind and draft writes. Reading configuration is allowed.
- Production UnsupportedProvider, JSON schema/API version/capabilities and
  --local-api-version output remain unchanged. No production test flags.
- No drafts, legacy CLI, recorder/solver, serializer/transaction, protocol,
  Bridge routing/Core/backend or later monitor/factory changes.
- Existing B2 freshness/TTL/cancel/commit semantics and writer deadline remain.
  The writer's2s grace starts after worker completion; it is not a provider or
  solver watchdog. Parent ownership/watchdog remains a later B3 packet.

## Required evidence

Two different selected native targets and transport/reference/readonly metadata;
deep lifetime after request/config mutation/destruction; every open validation
failure before configure; cached/mismatched/second open counts; configure failure
and terminal/new-worker contract; optional declaration; actual stdio fixture and
production no-resource bootstrap; canonical Envelope/Frame type preservation.

Run the accepted B2 26-CTest selection plus N0a cases/guard, B1/legacy byte proof,
all current/historical scope guards and unchanged old pins on final source.
Record actual counts/logs/SHA, remote equality, clean and required CI.
N0a adds a separate exact scope/pin and only mechanical connections to the B2
validator/fixture-copy set. Existing baseline pins remain byte-identical.

No dev/main merge. C01, actual capture, OS visual, B4/full parity and HIL remain
incomplete. PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED.
