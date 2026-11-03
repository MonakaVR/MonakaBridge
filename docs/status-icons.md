# Explicit local status icons

The SteamVR output adapter optionally loads `resources/icons/assignments.json`
once at Provider Init. This is bridge-owned local presentation data, separate
from the existing render-model selection and public input protocols.

The exact version 1 format is `{ "version": 1, "assignments": { "RUNTIME_SERIAL":
{ "DeviceReady": "REFERENCE", "DeviceSearching": "REFERENCE", "DeviceStandby":
"REFERENCE", "DeviceOff": "REFERENCE" } } }`. Runtime serials are opaque exact
keys; names, source identity and producer descriptions never select icons.

References must use `{monaka_bridge}/icons/` followed by a simple PNG/GIF
filename. Files must be regular, contained in this package's icons directory,
at most 1 MiB, and have the matching PNG/GIF signature. Configuration is bounded
to 128 KiB and 256 assignments. Duplicate/unknown fields and unsafe, absent or
malformed resources disable this optional map and log a diagnostic.

The four named-icon properties are set on device Activate, including for an
invalid pose. SteamVR chooses the displayed state. Missing assignments retain
generic defaults. Icons never change pose validity, freshness, routing, saved
World/Mount or render-model visibility. Loss stays invalid and recovery follows
the existing tracking path. Property/resource failures do not stop pose updates.
