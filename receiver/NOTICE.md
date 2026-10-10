CommonWorldAuthority.kt and TrustedHmdCommonWorldStream.kt are extracted from
MonakaVR/MonakaVR commit 299dda9d31d98ac28da018c5be2a916e36193566. The original
acceptance / Float projection / tombstone / ordered-session algorithms are kept.
5AI-R2 adds only explicit inactive recovery export/import methods to those files.
Source paths and original SHA256 values are in provenance.json. ktmath files are
unmodified and retain their MIT/Apache notices. The receiver does not include
Slime, body assignment, IK, device access, a live source producer or test sinks.

Wire decoding uses the unchanged actual hash-pinned MonakaProtocol 2.2 JVM JAR
from the accepted 5AI-P kit. No alternate decoder is implemented here.
