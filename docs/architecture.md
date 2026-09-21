# Common processing and responsibility boundaries

```text
PICO backend ─┐
             ├─ C1 29810 ─ Registry ─ Mapping/Profile ─ World/Mount ─ common cache
VIVE backend ┘                    │                                      │
                                 └─ Observation mirror 29813             ├─ MTP 29811
                                                                        └─ MTP feed 29812 → thin Direct driver
```

Only `MonakaProtocol::Codec` decodes/encodes C1. Vendored JSON in configuration/service health and test harnesses is not an alternative wire implementation. PICO private ABI/POTB decode and VIVE HID/RF decode are absent from production targets. The original extraction includes legacy transport dependencies for provenance and the test-only old TrackerDevice baseline; those files are restored under ignored `build/upstream` and never linked into the common service/driver.

World calibration reads exact native poses from the existing bounded Observation
mirror on 29813 and SteamVR reference poses from the standing universe. It does not
poll health JSON or require Direct output. Profile normalization precedes a proper
right-handed rigid solve; only explicit apply persists shared `world` rotation and
translation through the normal validated atomic config path.

Registry keys use `(source_id, device_id)`. Each source owns its current session, clock ID, retired sessions, peer admission and independent device state/pose sequence high-water marks. A restart clears only that source. Peer addresses are transient collision evidence, never persistent identity. A concurrent different peer for the same active source, or the same pose sequence carrying different content, fails closed and quarantines that source until Bridge restart. A legitimate backend restart that also changes its UDP peer must wait the configured inactivity timeout; immediate ambiguous peer replacement is intentionally not accepted.

Admission computes integer `age=sent_at_ns-timestamp_ns`, then fixes `local_timestamp=receipt-age`. Sender and receiver epochs are never directly subtracted. A sample predating the Bridge's local epoch is not made fresh. Device state cannot refresh pose age. Static numeric poses with increasing accepted sequence stay fresh. Default timeout is 500 ms, configurable from 1 to 10000 ms. Absence cancels pending pose output. Battery age is separately mapped from the state receipt time.

An explicit profile and exact approved input map/revision gate normalization. PICO compatibility independently maps position `(x,y,z)` unchanged and quaternion `(-z,y,-x,w)`. Unknown profiles and native VIVE default to blocked output. World transform then device-to-mount transform use Hamilton xyzw. A nonzero mount arm requires orientation for its position, and omega for offset velocity; offset acceleration remains null without the missing angular-acceleration/centripetal terms. Only derivatives with known C1 frame/evidence are considered. Unverified input angular mapping is discarded; generic quaternion history may derive omega using the bounded Task2 algorithm.

MTP has the persistent Bridge installation ID as `source_id`, a new Bridge session/clock UUID per process, and globally unique configured `tracker_id`. Its `input` preserves backend device/session/sequence. MTP pose sequence advances for an admitted observation or explicit mapping revision re-emission; re-emission retains original age. Session/config/profile changes reset derivative history. Reassignment of a previously used logical tracker to a different input during one Bridge lifetime is rejected; coordinated restart is required.

Runtime serial is `monaka-direct:` followed by UTF-8 hex of the Bridge installation ID, `:`, then UTF-8 hex of logical tracker ID. This length-unambiguous encoding avoids concatenation collisions. Distinct installation IDs are required when combining separate Bridge instances. A single installation rejects duplicate logical trackers even across backend sources.

Bounds: 64 sources, 256 devices per source, 1024 retired sessions per source; 256 configured/lifetime logical identities, 64 profiles; 512 latest pending messages per output channel; 32 messages flushed per channel per service iteration; at most 128 ingress datagrams per iteration. Overflow is rejected/counted. Outputs are nonblocking UDP; failures in one channel do not block another. The serialized ingress-to-egress benchmark measures the complete common pipeline with all three fan-outs, not hardware latency.

Direct keeps registered objects until SteamVR `Cleanup`, maps partial poses to invalid full SteamVR poses while retaining orientation metadata, and computes a local fixed-age time offset clamped to [-100 ms,+20 ms]. It owns registration, properties and pose submission only. Source transforms, calibration and diagnostic zero are not applied again in the driver. The standalone regression's mock OpenVR logger is test-only; normal SteamVR provider initialization supplies the real driver context.

Bridge owns profiles/calibration/identity/routing/common UI. PICO retains HMD runtime/Android/private protocol and vendor operations. VIVE retains HID/RF/native packets and device operations. MonakaVR/Task5 retains body roles, priority/fallback/fusion, IK and final solver output.
