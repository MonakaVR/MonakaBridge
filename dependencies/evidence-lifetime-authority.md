# MonakaVR — Astra / Codex Evidence Lifetime Contract 修正
## Physical Criteria Policy v1をProduction契約へ正しく載せ、R0-R9を再度閉じる

**フェーズ:** オフライン作業フェーズ  
**Hardware:** NOT_RUN  
**実環境変更:** 禁止  
**目的:** Policy v1の変換FAIL原因である「無期限/semantic validity」と「再起動後Evidence再利用」の契約不一致をProduction設計として解消する。

---

# 0. 現在の事実

直前のPolicy統合作業結果:

- Policy conversion: FAIL
- Latest HIL evidence evaluation: PASS
- Exact max position residual: `0.02095654506670834 m`
- Exact max orientation residual: `0.06659053187320611 rad`（約3.815 deg）
- inlier / all-pair max: 同値
- Reuse/invalidation tests: FAIL
- Expiry representation: NEEDS_DECISION
- Desired-state R0-R9: FAIL
  - R3/R9未達
  - R4-R7停止
- Actual environment changed: NO
- Production policy artifact: 未生成
- conversion attempt SHA256: `065cb76fb01ab415a2773fc257a1aa9376ed9e533f287acaa0e99d0d41b02529`

最新HIL evidence自体は既存の数値Gateを満たしている。

- Position RMS = 7.806 mm
- Orientation RMS = 1.217 deg
- Max Position = 20.957 mm
- Max Orientation = 3.815 deg
- 323 / 323 inlier

したがって今回の本質は**物理精度不足ではなくEvidence lifetime契約の不整合**である。

---

# 1. この作業の設計方針

以下を明確に分離する。

## 1.1 Measurement / capture freshness

Calibration中のsource/session/clock、current reference、Standing witness、liveness、capture timeoutは従来どおり短時間・厳格なfreshnessを要求する。ここを緩めない。

## 1.2 Output Review transaction lifetime

`review_output -> prepare_output -> commit_output` のtransaction/review deadlineは短時間のまま維持する。既存の10分review deadline等はEvidenceそのものの寿命とは別物として扱う。

## 1.3 Saved Physical Evidence validity

保存済みPhysical Evidenceは、**固定日数の経過ではなくsemantic continuityで有効/無効を判定**する。

単なるGUI restart、MonakaBridge restart、PICO PC provider restart、PC service restartだけでは、space continuityがSameでありimmutable provenanceが一致する限り失効させない。

## 1.4 Current approval witness

過去Evidenceを再利用してOutput Approvalする時でも、**approval時点のfresh current witnessは必須**。古いEvidenceを盲目的に承認しない。

---

# 2. Production契約の目標

現行 `OutputApprovalCriteria` / Authority / evidence schemaを監査し、Evidence validityとして最低でも以下を表現可能にする。

- `VALID`
- `INVALID`
- `UNKNOWN`

または同等のfail-closed表現。

### VALID
全semantic identity/provenanceが一致し、current witnessでcontinuityが確認できる。

### INVALID
input space revision、World、Mount、profile/capability、source/device、criteria version/hash、attachment relation、artifact hash等が意味的に変化した。

### UNKNOWN
continuityを証明できない。UNKNOWNはOutput Approval不可。

---

# 3. Expiry契約を修正する

## 禁止
24h / 7日 / 30日 / 90日等の任意期限を決めて回避しない。

## 推奨実装
現行absolute `expiresAt` がEvidence有効期限を必須にしているなら、以下の優先順位でProduction-correctに変更する。

1. optional expiry: `expiresAt = null` / absent がsemantic validityを意味する
2. explicit lifetime mode: `lifetime: semantic`、optional `not_after` はpolicy側で明示された時のみ
3. schema互換性が必要なら新schema versionを導入し、旧versionだけtimestamp必須、新versionでsemantic modeを正式サポート

最大timestamp等で「事実上無期限」を偽装しない。

必要なら以下を別概念へ分離する。
- criteria/policy validity period
- evidence semantic validity
- review transaction deadline
- fresh current witness window

---

# 4. Restart continuity

P0-B Revision Contractと統合する。

## PASSすべきケース
- GUI restart
- Bridge restart
- PICO PC Provider restart
- service restart
- sender session restart + declared coordinate space continuity = Same

これらの後でもsource/device identity、input space id/revision、World revision、immutable evidence hash、fresh current witnessが一致すればEvidence再利用可能。

## 必ず失効
- coordinate space generation changed
- revision changed for semantic reason
- continuity UNKNOWN
- World changed
- Mount changed
- profile/capability changed
- reference semantics changed
- Standing/world resetによりWorld continuity否定
- physical attachment relation changed
- evidence/result/config hash mismatch

---

# 5. Evidence reuse API

Production Authorityが過去Evidenceを単純timestampだけでrejectせず、`evaluateEvidence(currentContext, savedEvidence, policy)`相当の一箇所で判定する構造にする。

返却結果には少なくとも:
- status
- exact reason
- policy ID/version/hash
- evidence ID/hash
- current source/device/space/revision
- saved source/device/space/revision
- World revision
- continuity status
- current witness status
- optional time-policy status

を含める。silent fallback禁止。

---

# 6. Policy v1の変換

元Policyの意味を維持。

- paired samples >= 80
- valid timestamp coverage >= 70%
- inlier ratio >= 80%
- inlier position residual <= 0.05 m
- inlier orientation residual <= 0.15 rad
- position RMS <= 0.025 m
- orientation RMS <= 0.08 rad
- max inlier position <= 0.05 m
- max inlier orientation <= 0.15 rad
- all-pair maxはevidenceに保存するがv1 independent hard gateにはしない

最新Evidenceはexact Native値でPASSを確認する。

---

# 7. 必須テスト

## Lifetime
- no-expiry semantic evidence
- optional explicit expiry absent
- explicit expiry present and valid
- explicit expiry expired
- legacy timestamp schema migration/compat
- invalid lifetime mode
- malformed time

## Restart reuse
- GUI restart -> reusable
- Bridge restart -> reusable
- PICO provider restart same space -> reusable
- service restart same space -> reusable
- sender session changed but space same -> reusable
- space revision changed -> reject
- continuity UNKNOWN -> reject

## Semantic invalidation
- source changed
- device changed
- World revision changed
- Mount changed
- profile changed
- capability contract changed
- criteria hash changed
- evidence SHA changed
- result SHA changed
- reference semantics changed
- attachment declaration changed
- current witness unavailable/stale/mismatch

全ケースでexact reasonをassertする。

---

# 8. R0-R9再実行

契約修正後:
1. clean build
2. focused lifetime tests
3. Output Approval tests
4. Revision tests
5. full regression
6. saved HIL evidence replay
7. Policy v1 conversion
8. Production Output Approval replay
9. desired-state R0-R9

R3 PASS後、R4-R7を再開し、R9 desired-stateまでPASSさせる。

---

# 9. Production Policy artifact生成

全テストPASS後のみ:
- `monaka-physical-world-calibration-v1`
- version 1
- exact numeric criteria
- semantic lifetime mode
- schema version
- policy provenance
- exact SHA256

を持つProduction policy artifactを生成する。

状態は `READY_FOR_ADOPTION`。ユーザーがSHAを採用するまでは`ADOPTED`にしない。

---

# 10. Environment packageとの統合

直前のPre-HIL packageへProduction policy candidate、schema/version、replay receipt、current/desired/rollback state、one-shot executorを統合する。

**実環境applyは禁止。**

---

# 11. 自律ループ

compile/test/replay失敗では停止しない。

```text
LOOP:
  inspect exact failure
  patch minimal production-correct fix
  build
  focused tests
  regression
  saved-HIL replay
  policy conversion
  desired R0-R9
  repeat until PASS
```

人間へ聞いてよいのはrequirement contradiction、hardware required、destructive/remote action requiredのみ。

今回の「expiryを何日にするか」は質問しない。**semantic lifetimeを実装することが今回の承認済み設計目標**。

---

# 12. Done定義

以下が全てPASS:
- Policy conversion
- lifetime representation
- restart reuse
- semantic invalidation
- latest HIL evidence evaluation
- exact max residual evaluation
- Production Output Approval replay
- P0-B restart continuity regression
- Full regression
- Desired-state R0-R9

かつ:
- actual environment changed = NO
- Hardware = NOT_RUN

---

# 13. 最終成果物

- `EVIDENCE_LIFETIME_CONTRACT_REPORT.md`
- `EVIDENCE_LIFETIME_RESULT.json`
- `PHYSICAL_CRITERIA_POLICY_V1.json`
- `PHYSICAL_CRITERIA_POLICY_V1.sha256`
- `OUTPUT_APPROVAL_REPLAY_RECEIPT.json`
- `R0_R9_DESIRED_STATE_RECEIPT.json`
- updated `PRE_HIL_PACKAGE.zip`
- updated `NEXT_HIL_RUNBOOK.md`
- SHA256 manifest

---

# 14. 最終報告

```text
OVERALL:
READY_FOR_POLICY_ADOPTION
or
BLOCKED

Policy conversion:
PASS/FAIL

Latest HIL evidence:
PASS/FAIL

Max pos:
...

Max ori:
...

Lifetime contract:
PASS/FAIL

Restart reuse:
PASS/FAIL

Semantic invalidation:
PASS/FAIL

Production Output Approval replay:
PASS/FAIL

Desired R0-R9:
PASS/FAIL

Full regression:
PASS/FAIL

Policy artifact:
<path + sha>

Actual environment changed:
NO

Hardware:
NOT_RUN

Human action remaining:
1. adopt exact policy SHA
2. approve one-shot environment apply
```
