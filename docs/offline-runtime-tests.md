# Current runtime and historical launcher tests

The normal product starts the fixed repository `Build/runtime` output through
its external launcher. Explicit builds and current launcher acceptance verify
that contract independently of historical artifact catalogs and PASS receipts.

`offline_tools` tests the removed `scripts/launch_artifact_set.py`, dated-package
catalog selection and four historical compatibility/PASS receipts. It is
registered only through `MB_ENABLE_HISTORICAL_AUDIT` with the existing `Audit`
label. Its source remains unchanged for historical inspection; its deleted
launcher dependency is not restored. Historical execution is `NOT_RUN` unless
that complete historical dependency set is explicitly provided.

Excluding this obsolete catalog test from the normal lane does not establish
runtime acceptance. The current fixed-output wrappers, inert relocated external
launcher tests and actual product-path software evidence must stand on their own.
Physical HIL and deployment remain separate judgments.
