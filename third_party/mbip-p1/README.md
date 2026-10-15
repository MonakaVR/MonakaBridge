# MonakaBridge Input Protocol (MBIP)

Phase 2C-P1 provides the pure native/JVM [capability normalizer](docs/capability-mapping.md)
and its [explicit mapping table](schema/capability-mapping.json). MVIP vocabulary,
wire validation and channel projection come from one exact source/content-pinned
kit in `third_party/mvip`, verified by `python tools/verify_mvip.py` before native
build. MBIP does not maintain a second capability vocabulary or codec.

STRICT is the default. Legacy dynamic producers require an explicit structural
declaration and source/tracker/session binding; no validity/presence inference.
Normalization leaves identity, validity, age, space and mapping revisions intact.
This value-only contract does not implement the future DLL ABI, a production
receiver, a coordinate mapper or any Fusion/reconnect policy.

Build `cpp` with CMake into `build/native` (MSVC Release), then Gradle 8.14.4
`-p jvm build`. Run `python tools/generate_mapping.py --check`,
`python tools/generate_core_probe.py --check`, and `python tools/test_boundary.py`.
The boundary tests use actual MVIP C++/JVM serialization in opposite-language
MBIP consumers, and an exact source-pinned P0 Core capability comparison block.
The original old-Bridge sequence fails that comparison; provisioned normalization
passes it. This is bounded contract evidence, not a production rollout/HIL claim.

`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`

Standard input protocol for [MonakaBridge](https://github.com/MonakaVR/MonakaBridge).

MBIP defines the contract used by input modules that provide tracker observations to MonakaBridge. It is intended to keep device-specific backends and MonakaBridge decoupled while giving MonakaBridge one stable input boundary.

## Scope

This repository owns the MonakaBridge input contract, including:

- input-facing data structures and ABI/API contracts;
- protocol/version compatibility rules;
- capability and tracker metadata required at the input boundary;
- validation rules, fixtures, and conformance material for MBIP implementations.

The concrete ABI, field set, and versioning policy are intentionally not frozen by this initial repository bootstrap.

## Out of scope

This repository does **not** own:

- PICO, VIVE, Slime-class IMU, or other device-specific access;
- pairing, transport discovery, or vendor runtime control;
- MonakaBridge mapping, calibration, routing, or UI;
- Fusion/Fallback, IK, or body-role solving;
- SteamVR or other output implementations;
- the MonakaVR-side input protocol.

Those responsibilities belong to their respective backend, MonakaBridge, MonakaVR, or output-module layers.

## Architecture

```text
Device / Backend
      |
      v
MBIP input module
      |
      v
MonakaBridge
      |
      +--> Output module(s)
      |
      +--> MonakaVR path
```

MBIP is the input boundary **into MonakaBridge**. It should not accumulate downstream runtime responsibilities.

## Repository status

Bootstrap stage.

The first implementation work should define the minimum contract required by MonakaBridge and its input modules, then lock down ABI/version semantics with tests and fixtures before broadening the surface.

## Branches

- `main` — stable repository baseline
- `dev` — integration branch for active development
