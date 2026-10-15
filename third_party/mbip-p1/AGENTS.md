# MonakaBridgeInputProtocol repository instructions

This repository owns the MonakaBridge Input Protocol (MBIP): the standard contract used by input modules to provide observations to MonakaBridge.

## Repository boundary

- MBIP is the input boundary into MonakaBridge.
- Input implementations are DLL modules loaded by MonakaBridge.
- Keep device/vendor access and pairing logic in their backend implementations.
- Keep mapping, calibration, routing, UI, and output hosting in MonakaBridge.
- Keep Fusion/Fallback, IK, and body solving out of this repository.
- Do not move or duplicate the MonakaVR-side input protocol into MBIP.

## Contract discipline

- Do not invent or silently change ABI fields, units, coordinate conventions, timestamps, version semantics, ownership rules, or public symbol names.
- Treat ABI/API compatibility as an explicit versioned contract.
- Prefer the smallest contract that satisfies current MonakaBridge input requirements.
- Add fixtures and conformance tests when the contract is implemented.
- Do not add network/socket runtime services to this repository unless the architecture is explicitly changed.

## Git / task discipline

- Base active development on `dev` unless a task explicitly specifies another branch.
- Do not force-push, rebase shared history, amend unrelated commits, or discard unrelated work.
- Keep commits small and reviewable.
- Run applicable build and tests before reporting implementation work complete.
- Do not claim hardware validation unless hardware validation was actually performed.
