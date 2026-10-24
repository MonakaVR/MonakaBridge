# Independent review R1 — optional declaration boundary

The N1 RuntimeCaptureProvider incorrectly made capabilityProfile mandatory at configure/open. This changed accepted N0a behavior. Restore the original contract: a canonical config without mapping runtime_contract opens, including readonly; discover rejects EXPLICIT_DECLARATION_REQUIRED before started/owner setup, SDK initialization or mirror bind. There is no inferred/default declaration and no successful capture without one.

The production change only moves the existing requirement from validateConfiguration to discover. Address/port/reference-role validation stays at configure. Core, normalization, solver, session, UI and writer semantics remain unchanged.

The regression uses the concrete RuntimeCaptureProvider with CalibrationSession, public fake SDK/transport counters and a canonical config with runtime_contract removed from mappings. It covers writable/readonly open, discover refusal, zero SDK/bind/pose/event/shutdown calls, no drafts/config writes, and the direct concrete provider boundary. The actual product executable also runs through the same path with an isolated sentinel DLL; no genuine SDK/socket/HIL is used. The older fixture Provider's N0a test remains intact but is not the production acceptance proof.

Keep 76c4bbe reports, raw logs, baseline, package and ZIP unchanged. A separate exact R1 scope layer pins 76 and all thirteen accepted baseline files as raw bytes, checks guard ordering independently even after metadata hash redeclaration, and projects reviewed R1 bytes for older guards. Full accepted software45 remains required before replacement packaging/CMD references. Real SDK/OS visual/HIL and PICO same-tracker IMU authenticity remain NOT_RUN / NOT VERIFIED / DEFERRED.
