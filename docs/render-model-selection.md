# Explicit display resource selection

The SteamVR output adapter resolves an explicitly configured `model_id`. It does
not classify the input producer or infer a model from identity or descriptions.
No descriptor was added to MVIP/MBIP. The public kits have no display selector;
this change uses the existing bridge-owned local presentation configuration.

`monaka_prepare_presentation config health source device new-directory [model-id]`
prepares a separate snapshot. Omitting the ID produces no model assignment. An
ID contains 1–128 ASCII letters, digits, `_` or `-`. Preparation validates the
approved mapping and fresh provenance but does not claim installed asset validity.
Loading the driver validates the explicit ID against `resources/render-catalog.json`,
whose exact SHA is compiled into the driver. Catalog entries map opaque IDs to
asset-manifest SHA256 values. The manifest and all three assets must match; unknown,
missing, altered, or invalid resources disable appearance without altering Pose.

The existing `device_kind` field is retained as an opaque, bounded compatibility
string. New snapshots set it empty. Its contents never select a resource or change
tracking behavior. No producer-private files, processes, APIs, or installation
searches are consulted. IDs are used only for exact mapping/provenance matching.

The three inherited asset entries are migration data preserving previously
validated resources, not a product table in the resolver. Their names and bytes
are retained solely for compatibility and HIL traceability. Remove these bundled
entries after users migrate explicit snapshots to a separately reviewed generic
asset package; no automatic name substitution or guessed fallback is allowed.
Adding/removing a reviewed catalog entry requires rebuilding the package, not
changing resolver code. Asset redistribution authority is outside device discovery.

The +90 degree model-local correction remains in the unchanged assets. World,
Mount, Output Approval, Pose transforms, freshness and loss/reacquisition logic
are unchanged. Historical HIL results apply to their exact historical binary;
current binaries receive offline validation and require separate hardware cutover.

# Current rules and historical evidence

`dependencies/current-rule-hierarchy.json` identifies effective instructions and
their immutable source commits. `tests/integration_scope.py` checks their exact
bytes, required architecture clauses, and the absence of product-specific names
in resolver implementation. The committed integration receipt pins current edits.
Only after current admission do legacy audits receive their immutable historical
file view. Their old AGENTS checks prove historical HIL provenance, not permission
to use old architecture rules. Current functional tests compile actual files.
Receipt rehashing, current clause deletion, and new nested instruction files must
fail; historical receipts and their negative mutation tests remain intact.
