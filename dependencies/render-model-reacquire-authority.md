# MonakaVR — Astra / Codex RenderModel復帰ロジック オフライン修正

**フェーズ:** オフライン作業フェーズ
**Hardware:** NOT_RUN
**実環境変更:** 禁止
**目的:** 追跡喪失後、Pose追跡が復帰してもSteamVR上のRenderModel表示が復帰しない不具合を修正し、次回HILで表示復帰だけを再確認できる状態にする。

## 0. 最新HILで確定した事実

- World: 保存済み
- Output Approval: 保存済み
- 実追跡: 復帰する
- RenderModel表示:
  - 一度表示条件を外れると消える
  - 追跡が復帰してもモデル表示が戻らない
- HIL総合: PASSにはしない
- 追加実機操作: 不要

90° pitchずれについて:
- 未確定
- 現行モデルは単色
- 実機との軸合わせ未検証
- よって今回の修正スコープでは回転補正を入れない

## 1. 最優先方針

今回の修正対象は RenderModelの再表示/再有効化状態遷移のみ。

以下へ脱線しない。
- World
- Output Approval
- Calibration
- Revision契約
- Tracking pose fusion
- PICO input
- SteamVR driver registration
- モデル自体の90°回転
- FreeCAD geometry変更
- material/texture redesign

90° pitch疑惑はバックログとして保持する。

## 2. Root Cause監査

SteamVR Direct driverのRenderModel表示条件を調査する。

確認対象:
- device activation
- tracked / untracked / pose invalid state
- RenderModel property assignment
- property invalidation
- component visibility
- model/resource loading lifecycle
- SteamVR device pose update
- lost -> recovery
- expiry -> recovery
- deactivate/reactivate
- device reconnect

特に、VALID -> VISIBLE、INVALID -> HIDDEN、VALID AGAIN -> VISIBLE へ戻らず、HIDDENでstuckする具体的コードパスを特定する。

## 3. 正しい状態モデル

RenderModelの表示状態を一度だけ設定する初期化フラグに依存させない。

推奨状態:
- Uninitialized -> Visible: activated && valid
- Visible -> Hidden: invalid/lost/expired
- Hidden -> Visible: valid again
- Any -> Deactivated: device deactivation

毎frameの不要なproperty rewriteは避け、desired state / applied state / tracking validityを分離してedge transitionで更新する。

## 4. 必須試験

Mock OpenVR Host / driver fixtureで自動化。

### Basic
- activate + valid -> model visible
- valid continuous -> no redundant re-init

### Lost / Recovery
- valid -> lost -> valid
- valid -> expired -> valid
- valid -> invalid pose -> valid
- multiple lost/recovery cycles

期待:
- each recoveryでmodel visibleへ戻る
- dynamic poseも同時に更新
- logical device IDは維持

### Resource
- RenderModel property remains/reapplies correctly
- texture/resource lookup remains valid
- failed resource load does not corrupt pose state
- model visibility failure does not invalidate tracking pose

### Lifecycle
- activate -> lost -> recovery -> deactivate
- deactivate -> activate
- service/driver fixture restart
- tracker reconnect

### Regression
- synthetic Direct dynamic pose
- lost/recovery
- expiry/recovery
- Rev3 model property selection
- generic tracker properties

## 5. 90° Pitch疑惑の扱い

コード変更禁止。

現Evidenceでは:
- modelが単色
- model-local axesが視覚的に識別できない
- 実機とのforward/up axis matching未検証

したがって90° pitchをdriver/model transformへ入れない。

OFFLINE_BACKLOG.mdへ:
「RenderModel axis alignment verification: asymmetric/axis-marked diagnostic modelを使用し、physical PICO trackerのforward/up/rightとSteamVR model local axesをHILで比較する。実測前に90°補正を導入しない。」

必要なら診断専用モデル候補を作ってよい:
- +X/+Y/+Z marker
- front/up label
- asymmetric marker

ただしProduction modelへ適用しない。

## 6. 自律ループ

inspect -> reproduce with Mock Host -> identify root cause -> implement minimal fix -> build -> focused tests -> lost/recovery regression -> full driver regression -> repeat until PASS。

compile/test failureで停止しない。

## 7. Done定義

すべてPASS:
- valid -> model visible
- lost -> model hidden/invalid as intended
- recovery -> model visible again
- repeated recovery cycles
- pose recovery unaffected
- RenderModel property/resource intact
- no World/Approval/config changes
- full regression PASS
- 90° pitch transform unchanged

## 8. 成果物

- RENDERMODEL_REACQUIRE_ROOT_CAUSE.md
- RENDERMODEL_REACQUIRE_RESULT.json
- test receipts
- binary/package manifest
- updated NEXT_HIL_RUNBOOK.md
- updated OFFLINE_BACKLOG.md
- SHA256 manifest

## 9. 次回HIL Runbook

1. Start verified build
2. HIL Preflight PASS
3. Existing World / Output Approval validity確認
4. real tracker tracked -> model visible
5. intentional tracking loss
6. tracking recovery
7. model returns automatically
8. repeated loss/recovery 1回
9. 可能ならaxis-marked diagnostic modelでorientation確認
10. evidence保存

World再測定・Output Approval再実施は、current policyが失効を要求する場合だけ行う。

## 10. 最終報告

OVERALL: READY_FOR_RENDERMODEL_RETEST / BLOCKED
Root cause:
Fix:
Lost/recovery model state: PASS/FAIL
Pose regression: PASS/FAIL
Full regression: PASS/FAIL
90-degree pitch: NOT CHANGED / NOT VERIFIED
Actual environment changed: NO
Hardware: NOT_RUN
