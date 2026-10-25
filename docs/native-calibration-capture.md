# N1 native calibration input adapter

Base: accepted N0b `959a33e0847007b1d2ab7c5c7c8a12f8ea6554c4`.
This change implements the production provider used by `--local-api` without
changing the legacy CLI, canonical codec/P1 normalizer, solver, Core, writer,
wire protocol, or existing nine baseline manifests.

## Actual path and ownership

```
normal local API -> CalibrationSession -> CaptureOwner (one input owner)
  -> RuntimeCaptureProvider
     -> public OpenVR Background, Standing, prediction=0
     -> existing UDP mirror -> pinned c1 DecodeEnvelope -> canonical Envelope
  -> existing CaptureMonitor -> draft/trajectory solver -> immutable result
  -> same result/candidate/review token -> existing compare-and-swap commit
```

Construction, version, configure, and open do not initialize SDK, bind sockets,
or create drafts. Only explicit discover starts the input owner. Background init
precedes mirror bind, and failed init cannot bind a port. SDK and mirror cleanup
occur once on that owner, outside Session locks. No runtime fake flags exist.
The separately named test fixture links the same provider/codec/owner/solver
with fake public-SDK and in-memory Datagram implementations. It is not a product.

## Standing witness decision

The pinned SDK is Valve OpenVR commit
`0924064316de3effbcd1acf1e309182a2deb1c05` (v2.15.6). Its public header does not
promise identical bits on repeated raw-to-Standing reads. N1 therefore makes the
conservative choice of exact finite binary32 numeric equality against the fixed
baseline: signed zero compares equal; every other finite value change invalidates.
There is no new temporal tolerance. Finite values use normalized-zero bit keys
so DAZ/FTZ cannot silently turn a subnormal change into equality.

Structural matrix validation reuses `solveRigidAlignment` on the same four unit
basis points as legacy `ReferenceRigid`, with the existing maximum residual
`.001`, and additionally rejects a nonpositive determinant. This existing
numerical conversion bound is not a physical calibration or temporal threshold.
Reset/chaperone/universe events, SDK generation, HMD universe, role/index/serial,
connection, and pre/post Standing values are independent witnesses. Query time
over the existing 20ms pairing bound marks the reference invalid; query/poll gaps
over 50ms fail closed. Event and datagram batches each have a 128-item cap plus
one overflow probe. Serial is bounded to 4096 bytes.

False invalidation from a numerically changed equivalent SDK transform remains
possible. Polling and pre/post checks are not atomic and cannot prove that no
unobserved ABA or upstream event loss occurred. A later tolerance or stronger SDK
generation contract requires an explicit reviewed policy, not a guessed epsilon.

## Canonical mirror semantics

The provider consumes the provisioned declaration frozen in ProviderOpenOptions.
It neither infers support nor normalizes again. The Bridge already runs pinned
P1 before its raw mirror fanout. Every received selected canonical pose/state is
delivered to CaptureMonitor; foreign selected peer/session/clock/space/declaration
and explicit absence are sticky faults. Unrelated source/device traffic and
malformed packets are boundedly ignored. Existing mirror queue delay remains in
`sent_at`, rather than being refreshed on receipt. The existing fanout is latest
only: overwritten or unreceived upstream history cannot be observed by N1.

## Provenance and checks

`dependencies/calibration-openvr-sdk.json` pins header, import library, genuine
runtime DLL, license, and SDK commit. Configure fails on drift. Production entry
links the real SDK import library; generic/session fixture targets do not import
the SDK DLL. Isolated actual-executable tests replace only their copied DLL with
the existing test sentinel. They exercise bootstrap and init failure, never the
official DLL/runtime. The CLI parser/matrix test also runs with the sentinel.

`native_capture_scope.py` validates exact current bytes before projecting them
to N0b; its child has no import back to a historical parent. All nine old pin
files are compared as raw bytes with the fixed base. Mutant tests cover source,
old pins, rollback, base/presence/hash metadata, projection, SDK byte drift, and
protected source equality. Current-source checks are never memoized.

Actual SDK compile/link is separate from same-source fake runtime validation.
Real SDK execution, HMD/SteamVR, practical UDP/HIL, OS visual GUI, live config
mutation, and dev/main merge are not performed by this phase.

`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`
