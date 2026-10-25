# J0 native/control source integration

The dedicated integration combines native N1
`bf637c228c2f6fd4d972614660386702e194bdbb` and accepted WS01A
`1b02d3628e11254bb0584e546c8a17713a6e6e26`, with common base
`279a01363f47ad3564b5cd8b9346180c49ee41d3`. It does not merge dev/main or change
production provider, codec, normalization, solver, Core, writer, or WPF behavior.
U1/U2 functional GUI calibration is a subsequent change.

Only three inherited paths overlap: capability_runtime_scope.py,
capability_runtime_scope_tests.py, and config_write_scope_tests.py. Their
resolution retains native measurement validation and adds the control validator;
scratch inputs are the union, including native protected/UNCHANGED files.

The new leaf `integration_scope.py` validates every actual composite file before
historical projection. Its explicit source list covers both parent deltas and
the reviewed mechanical connectors. All eleven previous baseline pins are raw
immutable bytes from their named parent; three shared pins are checked against
both parents. Each source entry records current digest and separate presence/hash
for native and control. The three resolutions have two distinct Git blob views;
there is no last-wins parent dictionary or automatic bootstrap reference.

```
common capability guard -> native measurement chain -> N1 -> J0 leaf
                        -> control foundation ------------> J0 leaf
```

The leaf imports no parent. Only immutable Git-object reads are memoized; current
bytes are read again at every approval. Parent validators consume their named
predecessor view after current approval. Historical projection does not stand in
for current composite build/runtime tests. Independent AST checks retain both
delegations and the named native/control connections, even in tests that update
a mutant's metadata hash. Missing scratch inputs reject before testing mutants.

Tests cover all current sources and old pins, both parent identities and blob
hash/presence, overlap rollback, unknown/foreign projections, lost delegation,
wrong view, missing current source, and the complete inherited scope suites.
The CMake registration adds the J0 scope test without deselecting native tests.

Validation reports distinguish native software tests, WPF compile, inherited
scope tests, and OS visual validation. Real SDK execution, SteamVR/HMD, practical
UDP/HIL, live configuration edits and dev/main merge remain outside this work.
The old UI smoke's CreateDesktop/NotifyIcon paths are not an alternative to the
unavailable OS visual gate and are not claimed as headless execution.

`PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED`
