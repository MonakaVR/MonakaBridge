# MonakaVR / MonakaBridge PR #13 — HIL Preflight P1 Fix
## Producer-private process dependencyを除去し、public MBIP contractベースのreadiness判定へ修正する

**フェーズ:** オフライン修正 / PR remediation
**対象repo:** `MonakaVR/MonakaBridge`
**対象PR:** #13
**対象branch:** `fix/final-integration-blockers-20261025`
**レビュー対象HEAD:** `e857c5bcf2e38b9f18a9601f98a12fab21afbb65`
**Base:** `dev`
**Hardware:** NOT RUN
**実環境/配布物:** 変更禁止
**PR merge:** この指示内では禁止

## 0. 現在の状態

PICO:
- PR #11: merged to dev
- dev merge commit: `e82f44b`
- このタスクではPICO repoを変更しない

MonakaBridge:
- PR #13: OPEN
- CI: PASS
- merge: HOLD
- Codex automated review: P1 findingあり
- finding path: `scripts/hil_preflight.py`
- finding line: 73付近
- unresolved review threadあり

P1:
- HIL preflightが特定MBIP producerをexe名で識別
- `ExecutablePath` とSHAを取得
- `expected['services']` と比較
- public contractが同等でもproducer差替え/更新でreadinessがFAILし得る
- current AGENTSのMBIP input independenceに違反

## 1. 最優先アーキテクチャ制約

MonakaBridgeはproducer-private情報を参照しない。

禁止:
- producer-private executable
- producer process列挙
- producer install path
- producer executable SHA
- producer名/vendor名でbranch
- producer lifecycle/control/config access

特に current HIL readinessで以下を要求しない:
- `pico_ot_bridge_service.exe`
- PicoMotionTrackerBridge private path/SHA
- producer-private process inventory

## 2. 違反箇所

現行 `scripts/hil_preflight.py` はWindows inventoryで
`monaka_bridge_service.exe` と `pico_ot_bridge_service.exe` を同時に列挙し、
両者のpath/SHAを `actual['services']` へ入れている。

その後 `expected['services']` と比較し、producerのprivate build identityまでreadiness条件にしている。

## 3. 修正目標

### 3.1 Bridge-owned runtime verification
Bridge自身が所有するものだけを確認:
- `monaka_bridge_service.exe`
- Bridge helper
- Bridge config
- Bridge SteamVR driver package
- OpenVR runtime/profile
- Bridge-owned registration
- package receipts / approval artifacts

Bridge-owned executableのpath/SHA/build identity検査は維持可。

### 3.2 Public input readiness
外部producerはprocess/exe/path/SHAを見ない。

Bridgeのpublic/native Observation由来Health snapshotだけを使う。

利用可能なgeneric fields:
- publisher_id / publisher_session
- mapping_revision / snapshot_unix_ms
- source / device
- input_session / input_clock
- space / convention / revision
- fresh / tracker / collision
- tracking_state
- position_valid / orientation_valid
- has_pose / absent
- pose_age_ms / source_receive_age_ms
- pose_sequence / state_sequence

## 4. Public-contract readiness contract

preflight expectationはvendorではなくopaque identityで指定する。

例:
```json
{
  "input_readiness": {
    "source": "<opaque>",
    "device": "<opaque>",
    "tracker": "hip",
    "input_space": "<opaque>",
    "input_revision": 112
  }
}
```

vendor推定禁止。

selected inputのPASS条件:
- exactly one source/device
- fresh
- has_pose
- tracking_state == tracked
- position_valid
- orientation_valid
- collision == false
- absent == false
- expected space
- expected revision
- expected tracker
- expected mapping_revision
- Health snapshot fresh

input session/clock/sequenceはdiagnosticとして保持可。

Health missing/malformed/stale/future/config mismatchはFAIL。

producer process存在は条件にしない。

## 5. Producer replacement equivalence

以下は同じ判定になること:

Producer A (exe/path/SHA=A)
→ valid public MBIP observations

Producer B (exe/path/SHA=B)
→ same valid public MBIP observations

Bridge preflightは両方PASS。

## 6. `services` schema

Bridge + producer混在なら責務を修正。

推奨:
- `bridge_services`
または意味的にBridge-ownedのみと明確なfield

互換readが必要なら限定的に実装。
producer service entryはcurrent readiness評価に使わない。

古いexpected JSON互換のためproducer-private checkを残すことは禁止。

## 7. 必須tests

### Bridge-owned
PASS:
- exact Bridge service path/SHA

FAIL:
- Bridge service absent
- wrong path
- wrong SHA
- helper/config/runtime/driver mismatch

### Producer independence
必ずPASS:
- producer process名変更
- producer exe path変更
- producer exe SHA変更
- producer process inventory取得不能
- producer implementation/vendor変更

public Healthが同じならPASS。

### Public input readiness
PASS:
- opaque source/device exact match
- fresh/tracked/PQ valid
- matching space/revision/tracker
- no collision

FAIL:
- health missing/stale
- selected input missing/duplicate
- lost/stale
- P invalid/Q invalid
- collision/absent
- wrong space/revision
- wrong mapping revision
- config mismatch

### Vendor-neutral fixture
PICOという名前を含まないgeneric producer fixtureを追加しpublic contractのみでPASS。

## 8. Static audit

current production/script/test readiness logicに
- `pico_ot_bridge_service`
- `PicoMotionTrackerBridge`
- producer-private process filtering
が残っていないこと。

historical evidence/doc引用はcurrent behaviorと分離されていれば可。

## 9. Docs

`docs/offline-hil-readiness.md` 等を更新し、
- Bridge-owned service identity
- public MBIP input readiness
を分離して記述。

## 10. PICO repo変更禁止

PICO #11 / dev `e82f44b` は固定。

この修正のためPICO側へ:
- private API
- process reporting
- exe SHA reporting
- compatibility hack
を追加しない。

既存Healthで足りる限りpublic protocolも変更しない。

## 11. 回帰

修正後:
1. focused preflight tests
2. Bridge full regression
3. PICO dev 14/14 compatibility regression
4. lifecycle
5. stored HIL replay
6. RenderModel selection/reacquire/axis tests
7. final integration scope tests
8. clean build
9. diff audit

期待:
- Bridge current baseline以上 + new tests ALL PASS
- PICO 14/14 PASS
- saved HIL replay PASS

## 12. PR #13更新

全ローカル検証PASS後のみ:
- existing branch `fix/final-integration-blockers-20261025` に追加commit
- force push禁止
- PR #13更新
- CI完走確認
- P1 review threadへroot cause/fix/testsを返信
- Codex再レビュー要求

再レビューclean前にmergeしない。
review threadを手動resolveしただけで解決扱いにしない。

## 13. Motion prediction

今回scope外。
`OPEN / NOT INCLUDED`

以下を混ぜない:
- quaternion filter
- angular velocity publication
- acceleration publication
- SteamVR prediction changes
- PICO output B解析

## 14. Done

- producer-private dependency removed
- Bridge-owned verification preserved
- public MBIP Health readiness PASS
- vendor-neutral replacement test PASS
- Bridge regression PASS
- PICO 14/14 PASS
- lifecycle PASS
- saved HIL replay PASS
- CI PASS
- Codex re-review clean
- blocking reviewなし
- PR #13 mergeable
- PR #13 not merged

最終判定:
`READY_FOR_MONAKABRIDGE_PR13_MERGE`

## 15. 最終報告

OVERALL: READY_FOR_MONAKABRIDGE_PR13_MERGE / BLOCKED
Producer-private process dependency: REMOVED/REMAINS
Bridge-owned runtime verification: PASS/FAIL
Public MBIP input readiness: PASS/FAIL
Vendor-neutral producer replacement: PASS/FAIL
Bridge regression: <pass>/<total>
PICO dev regression: 14/14 PASS/FAIL
Saved HIL replay: PASS/FAIL
CI: PASS/FAIL
Codex re-review: CLEAN/FINDINGS
PR #13: OPEN / MERGEABLE / NOT_MERGED
Motion prediction: OPEN / NOT_INCLUDED
Hardware: NOT_RUN
Environment/distribution: UNCHANGED
