# MonakaVR — RenderModel Axis Alignment 確認・補正指示
## 90° Pitch疑惑を、追跡Pose本体と分離して検証する

**対象:** SteamVR RenderModelのみ  
**追跡Pose / World / Output Approval:** 変更禁止  
**開始フェーズ:** オフライン準備  
**実機フェーズ:** 診断モデル準備完了後のみ実施

---

# 0. 現在の確定状態

直前HILで以下は実機PASS済み。

- 実PICO TrackerのPose出力
- World calibration / 保存
- Output Approval
- SteamVR Direct実追跡
- tracking lost -> recovery
- RenderModel lost -> recovery
- 反復lost/recovery
- 最終100/100 Pose valid

RenderModel reacquire defectはクローズ済み。

未確認なのは以下のみ。

- RenderModelの実機軸合わせ
- 90° pitchずれ疑惑

現行モデルは単色のため、
視覚だけではforward/up/rightの識別性が不足しており、
90°ずれは未確定。

---

# 1. 最優先原則

今回の検証対象は **RenderModelの見た目の座標軸** のみ。

以下を変更しない。

- tracker Pose quaternion
- tracker position
- World transform
- Mount
- Output Approval
- Calibration
- Input revision
- Fusion / IK
- PICO Backend
- coordinate convention
- SteamVR Direct tracking logic

仮に90°ずれが確認されても、
**tracking Poseそのものへ補正を入れない。**

補正対象は、
RenderModel resource / model-local transform / model metadata
の範囲に限定する。

---

# 2. オフライン静的監査

最初にRenderModelの座標変換経路を監査する。

確認:

- PICO実機で想定する
  - forward
  - up
  - right
- source FreeCAD / export modelのlocal axes
- SteamVR RenderModelが期待するlocal axes
- driverから渡しているPoseのcoordinate convention
- model asset側に追加transformがあるか
- manifest/resource/model metadata内のtransform
- export時のaxis conversion
- 既存90° rotationの有無

必ず次を分離して報告する。

1. Tracking Pose convention
2. RenderModel local axes
3. Asset export axes
4. SteamVR representation

「見た目が90°ずれている」からPose補正を推定しない。

---

# 3. 軸マーカー付き診断モデルを作る

Productionモデル本体は変更しない。

診断専用モデルを用意する。

最低限:

- +X: `+X / RIGHT`
- +Y: `+Y / UP`
- +Z: `+Z / FORWARD`
- FRONT面を明示
- TOP面を明示
- 左右非対称な目印
- 可能なら実機Type-C口側など、
  physical trackerで方向を確実に認識できる特徴も表示

色の使用は可。
実機HILで一目で方向判別できることを優先する。

Production modelとは別resource名にする。

例:
`pico_tracker_rev3_axis_debug`

---

# 4. Mock / Offline確認

実機前にMock OpenVR Hostで:

- diagnostic model property selection
- resource load
- model asset existence
- normal tracked state
- lost -> recovery
- diagnostic modelがrecovery後も再表示
- original production modelへ戻せる

を確認。

また、既存RenderModel reacquire 41ケースおよびfull regressionを再実行。

軸マーカー追加のためにtracking testを壊さない。

---

# 5. 実機HIL — Axis Alignment

オフライン準備がPASSしたら、実機検証フェーズへ移行。

## H0 Preflight

- verified build/package
- World valid
- Output Approval valid
- tracker tracked/PQ valid
- diagnostic model selected

既存World/Approvalを再測定しない。

## H1 Neutral orientation

実機Trackerを、
物理的に基準方向が明確な姿勢へ置く。

例:
- Type-C口
- logo面
- top面

など、実物上で方向が確定できる面を使用。

記録:
- physical forward/up/right
- SteamVR model +X/+Y/+Z
- tracker quaternion
- screenshot / observation notes

## H2 Pitch

実物を約+90° pitch。
modelの対応軸が同じ方向へ回るか確認。

## H3 Yaw

実物を約+90° yaw。
modelが対応して回るか確認。

## H4 Roll

実物を約+90° roll。
modelが対応して回るか確認。

精密角度測定は不要。
軸交換・符号反転・固定90° offsetの識別が目的。

---

# 6. 判定

## PASS

physical trackerとdiagnostic modelの:

- forward
- up
- right

が一致し、
pitch/yaw/rollの各操作が期待軸へ対応。

判定:
`RENDERMODEL_AXIS_ALIGNMENT_PASS`

Production modelに回転補正不要。

## FAIL — Fixed model-local offset

Tracking Poseは正しいが、
RenderModelだけ一定90°等ずれていることが確認された場合:

判定:
`RENDERMODEL_MODEL_LOCAL_OFFSET_CONFIRMED`

補正はRenderModel側だけへ入れる。

例:
- asset export transform
- model-local transform
- resource metadata

のいずれか最小責務箇所。

tracking Pose quaternionは変更禁止。

## BLOCKED

軸識別不能、
diagnostic modelが正しくロードされない、
physical基準方向を確定できない場合。

実装へ推測で進まない。

---

# 7. FAIL時の自律オフライン修正

固定offsetが実機で確認された場合のみ、
実機フェーズを終了してオフラインへ戻る。

Codex/Astraは:

1. Evidenceからexact axis mappingを整理
2. 最小model-local correctionを選定
3. Production RenderModelへ適用
4. diagnostic modelにも同じ基準を反映
5. Mock
6. lost/recovery
7. full regression
8. 次回axis-only retest package作成

を連続実行する。

実機中にその場で修正しない。

---

# 8. 成果物

- `RENDERMODEL_AXIS_STATIC_AUDIT.md`
- `RENDERMODEL_AXIS_DEBUG_MODEL_MANIFEST.json`
- `RENDERMODEL_AXIS_OFFLINE_RESULT.json`
- `RENDERMODEL_AXIS_HIL_REPORT.md`
- `RENDERMODEL_AXIS_HIL_RESULT.json`
- screenshots / notes
- `RENDERMODEL_AXIS_BACKLOG.md`
- SHA256 manifest

---

# 9. Done定義

以下のいずれか。

### A. Alignment PASS
- axis alignment実機確認
- Production model補正不要
- issue close

### B. Fixed offset confirmed
- exact offsetを実機Evidenceで確定
- tracking Poseは正常
- RenderModelだけの問題として分離
- オフライン補正へ送る

### C. BLOCKED
- 推測補正せずEvidence不足を明示

---

# 10. 最終報告

```text
OVERALL:
PASS / MODEL_LOCAL_OFFSET_CONFIRMED / BLOCKED

Tracking Pose:
UNCHANGED / VALID

Physical forward:
...

Model forward:
...

Pitch mapping:
PASS/FAIL

Yaw mapping:
PASS/FAIL

Roll mapping:
PASS/FAIL

Confirmed fixed offset:
NONE / <exact transform>

World:
UNCHANGED

Output Approval:
UNCHANGED

Software modified during HIL:
NO
```
