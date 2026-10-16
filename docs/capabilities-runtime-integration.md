# Static capability propagation on the accepted P1 runtime

Base: `e08b95f8e147d75a9d48c4eb9e747a1db9194c1a`.
Reused existing Issue #2 work: `0d582284ea732f22f322a0558b9b9739374bd889`.

The existing patch provides Registry metadata and a static output projection.
An additional actual Registry test reproduced its rejection of the second legacy
advertised subset. This branch stores the canonical P1-normalized declaration.
STRICT sources must match their explicit source declaration. LEGACY_DYNAMIC
sources obtain support only from the explicitly provisioned declaration; raw
packet subsets never create a second support policy. A Q-only source needs its
own Q-only declaration, not a wider Pos+Q legacy profile.

The production path remains decode/authorization -> canonical MBIP P1 Normalizer
-> Registry declaration -> unchanged calibration -> static output projection
-> canonical output Normalizer -> Fanout. Input/output codec, wire versions,
identity, sequence/clock/space and calibration math are unchanged.

Pose and TrackerState use the same support set. Missing derivative values do not
remove support. Output derivative declarations are bounded by the provisioned
output contract and existing conversion implementation: orientation may produce
mapped angular velocity; translated mounts cannot promise linear acceleration
with the current math. Changes to conversion support on an existing logical
identity require a coordinated restart, rather than changing the Core lifetime.

No declaration is emitted before the first accepted backend declaration.
Once established it survives stale/absent/invalid data and backend session
changes; samples and derivative history still invalidate normally. Conflicts
fail closed while retaining the previous supported-but-invalid output contract.
Metadata arrival never changes mapping/world/config revision. Retired source
sessions remain rejected by the existing Registry lifecycle.

The previous config transaction and P2R baselines stay byte-exact. The additional
capabilities-runtime baseline enumerates and pins only fourteen reviewed paths.
Historical projection is used for historical byte comparisons; current-source
pins and every existing authority/math/5S assertion still execute. Drift and
predecessor-rollback controls must fail.

Software-only work. No device/SteamVR operation or production cutover.
Admission Gate remains default OFF; CANDIDATE effective validity stays false.
PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED.
