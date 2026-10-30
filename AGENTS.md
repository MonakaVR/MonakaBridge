# MonakaBridge repository instructions
## Current contract ownership (2026-10-18)

MVIP (MonakaVR/MonakaVRInputProtocol) owns the wire/model/schema/codec and capability vocabulary. MBIP (MonakaVR/MonakaBridgeInputProtocol) owns the pure normalization adapter over its fixed MVIP kit. Broader input DLL hosting is a later phase. Older MonakaProtocol ownership and fixed-v1 handoffs below are historical; use the actual hash-pinned kits in this tree and docs/responsibility-refactor.md. Do not change wire/ports or vendor/solver boundaries for this UI repair.


This repository owns the generic bridge/router between independent MBIP input producers and downstream tracking outputs. It accepts public TrackerObservation / ObservationDeviceState messages, maintains input identity and capability/state catalogs, performs generic coordinate-space calibration and logical tracker mapping, applies output policy, and publishes normalized MTP / MonakaVR and SteamVR Direct output.

## Highest-priority architecture invariant: MBIP input independence

**Mandatory for all work**, including code, configuration, GUI, tests, scripts, packaging, documentation, and migration. This invariant supersedes historical tasks and existing implementation patterns when they conflict.

The bridge MUST operate with any conforming MBIP input producer **without knowing what implementation, device family, vendor, manufacturer, model, transport, or software produced the input**. It MUST NOT depend on the presence or behavior of any particular producer. Producer identity is data, not a switch selecting product-specific logic.

### Strict prohibitions at the MBIP input boundary

- MUST NOT import, link, embed, or reimplement any input-producer-private API, ABI, SDK, device protocol, packet decoder, hardware transport, discovery mechanism, device control, or tracking runtime.
- MUST NOT discover, install, start, stop, restart, terminate, pair, configure, or otherwise control an input producer or its physical devices. Producer lifecycle, native coordinate interpretation, and device-specific acquisition/recovery are producer-owned.
- MUST NOT access producer-private executables, runtime state, configuration files, processes, installation paths, or undocumented APIs in the common bridge pipeline.
- MUST NOT branch on producer kind, manufacturer/model name, descriptive device strings, driver or application names, or magic prefixes/values within `source_id`, `device_id`, `publisher_id`, or `tracker_id`. Identity values may be used as **opaque keys** for cataloging, matching, deduplication, and explicitly configured mapping, but never to infer vendor, capability, coordinate conventions, or fallback policy.
- MUST NOT introduce named producer/device-specific if/switch branches, built-in source-specific coordinate fixes, output model/icon rules, routing exceptions, or hard-coded per-producer presets in common source code.
- MUST NOT infer missing capabilities, modalities, coordinate spaces, device descriptors, or render resources from recognizable identity strings. Do not substitute an observation's transient field presence for a declared static capability.
- MUST NOT silently assume unknown, undeclared, or unverified input coordinate spaces are identity transforms.
- MUST NOT implement consumer-owned body assignment, Main/Fallback selection, Fusion/Fallback, or IK inside the bridge, including the SteamVR Direct path.
- MUST NOT copy upstream-specific implementation guidance from legacy task notes into the common pipeline. Treat incompatible examples as historical, not architectural precedent.

These prohibitions apply equally to indirect or hidden dependencies introduced through helper processes, application UI, config readers, plugins, scripts, or test-only adapters later promoted to production.

### Permitted generic bridge responsibilities

- Receive, parse, validate, and admit messages solely through the active, versioned public MBIP contract; maintain source/device identity, capabilities, validity, freshness, sessions, and provenance.
- Discover *announced* input devices via generic observations, register them in the bridge's own catalog, and map physical input identities to bridge-owned logical tracker IDs.
- Perform contract-defined coordinate normalization, validated generic calibration/profiles, mapping, and output routing without interpreting a producer's private implementation.
- Preserve declared static capabilities separately from runtime tracking/pose validity. Pass declared descriptive metadata as data without vendor-specific interpretation.
- Manage the bridge's own configuration, connection endpoints, sessions, output policies, and diagnostics. These must not become producer configuration or producer process control.
- Send normalized MTP output to MonakaVR and publish logical trackers through the generic SteamVR Direct driver. Output protocol and platform-specific conversion is permitted **at the respective output adapter**, not at the MBIP input boundary.

The bridge is not required to be a byte-for-byte relay: generic admission, coordinate alignment, logical identity allocation, calibration, and routing remain bridge responsibilities.

### Device descriptors and SteamVR resource handling

- An input producer is responsible for declaring its device-specific properties and optional external or packaged display-resource references **through the public contract when that contract supports them**. Do not assume such fields already exist.
- The bridge may preserve and forward public device descriptors and apply them through a generic output adapter, including SteamVR properties for manufacturer/model strings, device class, status icons, and render models.
- Manufacturer/model strings, asset identifiers, resource namespaces, and paths are **opaque descriptor values**. They MAY be passed to the corresponding output property/resolver, but MUST NOT influence tracking, calibration, mapping, routing, capability classification, or producer selection.
- A resource resolver MAY validate and resolve resources by a generic documented scheme, independently of any producer identity. It MUST NOT embed product-specific installation searches, namespace substitutions, resource tables, or asset-selection conditions.
- Do not load arbitrary untrusted paths, perform unapproved file operations, or copy/repackage proprietary resources as a side effect of device registration. When a referenced resource is absent or cannot be validated, follow the generic fallback/diagnostic policy.
- If a required descriptor field or transport capability is absent from the public protocol, stop at that contract boundary and coordinate a versioned protocol change. Never invent local incompatible fields or back-channel producer-specific workarounds.

### Name and documentation rule

- **This AGENTS.md MUST NOT enumerate or mention concrete MBIP input producers, vendors, manufacturers, device families, hardware models, or upstream application names.** Refer to them only as `MBIP producer`, `input source`, `physical device`, or other abstract roles.
- Names of **standard downstream interfaces and explicitly supported output platforms**, including MonakaVR, MTP, SteamVR, and OpenVR, are permitted because they describe the output contract rather than an input producer.
- Specific upstream names in older documents, sample configuration, or existing code do not grant an exception. If legacy source-specific presets must remain temporarily, keep them isolated as migration data, not hard-coded branching, and record their removal plan.

### Mandatory design review and acceptance gates

For changes affecting the input boundary, routing, calibration, output adapter, configuration, UI, or lifecycle, run only the checks applicable to the edited behavior and contracts:

1. Confirm the bridge starts and accepts generic input without any particular producer installed, running, or configured.
2. Feed equivalent public-contract messages from two synthetically distinct producer identities. Verify they follow the same code path and produce equivalent normalized outputs when explicitly given equivalent policies and calibration.
3. Verify `source_id`/`device_id` are used only for identity lookup/mapping, not for producer classification or implicit capabilities/coordinate transformations.
4. Verify unknown producer/model strings and absent optional device descriptors do not change pose processing or silently enable a special case.
5. Verify no producer-private API, process/configuration access, device control, transport decoder, or output-resource product lookup has been introduced.
6. Verify capabilities vs. dynamic validity, coordinate-profile rejection, disconnection/reacquisition, identity/session provenance, and prevention of output-to-input feedback loops.
7. Verify MonakaVR and SteamVR Direct output paths remain distinct adapters over the same logical tracker state. Do not claim physical-device HIL without actually performing it.

Violations are **architecture defects and release blockers**, not optional cleanup. Mark unexecuted checks as `NOT RUN`; do not present them as PASS.

## Product-software verification and historical audit (2026-10-30)

MonakaVR is a producer-independent 6DoF body-constraint extension, not a physical-tracker quality certification tool. Normal software PASS must demonstrate actual public-input handling, transforms, IK/fallback/output or the relevant backend function; version/source hashes and historic approvals do not substitute for that behavior.

`scripts/test_revision.ps1` uses `-Mode Quick` and `-Mode Full` for product-software checks and `-Mode Audit` for historical source/release-policy checks. Audit may be used for a relevant release, dependency or policy migration, but must not be imposed as a default per-edit requirement. See `docs/verification-lanes.md`. Software PASS is not hardware HIL PASS. The existing mandatory calibration approval/revision model is a migration target under MonakaVR/MonakaBridge#17, not the intended long-term product acceptance criterion.

## Current coordinated Architecture Revision

The older Task 4 document and its fixed C1/wire-1.0 handoff are historical baseline material for the delivered implementation. For the current coordinated revision, protocol/identity/modality/Main-Fallback semantics are superseded by the Architecture Revision owned in `MonakaVR/MonakaProtocol` (`docs/architecture-revision.md` and `docs/C2.md`). Do not revert current v2 semantics merely to satisfy an older Task 4 paragraph.

Protocol-dependent migration work must use an **actual generated and hash-pinned wire-v2 kit** from a clean committed MonakaProtocol source tree. Until that artifact and manifest are supplied to this workspace, inspect/plan/refactor contract-independent code freely but stop at the protocol dependency boundary. Do not fabricate hashes, infer v2 fields from v1, or hand-roll a substitute codec.

Where the Architecture Revision does not conflict, existing Task 4 calibration, mapping, Direct-output, provenance, and hardware-gate constraints remain applicable.

## Codex task entry point

For bridge history and non-conflicting Task 4 constraints, read `docs/codex/Task4_MonakaBridge.md` and `docs/codex/task4/00_upstream_handoff_status.md` completely before implementation. Treat their fixed-C1 sections as historical once the coordinated v2 handoff is active. Historical examples do not override the input-independence invariant.

## Codex Cloud branch handling

Codex Cloud may expose its writable checkout as an internal branch named `work`. This is allowed when the checkout ancestry derives from the prepared `refactor/monaka-layer-separation` branch. Do not fail only because the local branch name differs, and do not rewrite history to recreate a branch name inside a managed checkout.

When continuing an existing branch, incorporate the current input-independence instructions through a history-preserving update or reviewed documentation-only backport. Do not merge unrelated changes merely to propagate this file or rewrite fixed HIL/review baselines.

## Repository-wide constraints

- Work only in this repository unless the active coordinated task explicitly states otherwise.
- Do not redefine shared wire fields, protocol types, ports, version rules, units, or public API names. Use the fixed active MonakaProtocol kit.
- Preserve modality and provenance from input observations; the bridge does not invent rotation-only capability or synthetic IMU streams.
- SteamVR Direct remains one normalized logical tracker path and must not perform MonakaVR cross-source fallback composition.
- Preserve provenance, licenses, notices, and source-path/hash metadata when importing permitted upstream material.
- Preserve unrelated existing work and uncommitted changes.
- Do not delete branches or files, force-push, rebase, amend, reset away work, or otherwise rewrite history.
- Keep commits small and reviewable.
- Mark build, mock, compatibility, and hardware validation independently. Never claim hardware cutover when it was not actually performed.

If repository state, the active v2 kit, or an upstream artifact conflicts with the current Architecture Revision or the input-independence invariant, stop at the conflicting dependency boundary, record the discrepancy, and report it rather than inventing a replacement contract or silently adapting behavior.
