# Codex Task 4: MonakaBridge

単独投入用実装指示書。Task 4 は `MonakaVR/MonakaBridge` のみを変更する。

**契約状態：Task 1 fixed candidate / wire 1.0 / master reconciliation pending。** C1の唯一の規範は固定Task 1 protocol kit内の `docs/C1.md`、schema、public codec/model APIである。このTaskでC1を変更・再定義しない。

開始前に必ず以下を全文読む。

1. `AGENTS.md`
2. `docs/codex/task4/00_upstream_handoff_status.md`
3. 本ファイル
4. supplied Task 1 kitの `docs/C1.md` / `protocol.lock.json`
5. supplied Task 2/3 handoff manifest/report

## Repository / audit

| 項目 | 値 |
|---|---|
| 対象repo | `MonakaVR/MonakaBridge` |
| default branch | `main` |
| audited base branch | `main` |
| audited base SHA | `686392ead354292cab02422c942bf0236dee4255` |
| prepared work branch | `refactor/monaka-layer-separation` |
| C1 SHA256 | `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3` |

元repoは初期commitと`.gitkeep`だけの空実装から開始する。

Codex Cloudでlocal writable branchが`work`でも、checkoutがprepared branchのdescendantなら許可する。branch名だけを理由に停止しない。逆に監査base/prepared branch由来でない場合は実装せず差分を報告する。

branch/file削除、force push、rebase、amend、resetによる履歴差し替えは禁止。mainへ直接実装しない。

## Task 4の責務

共通adapter/routerとして以下を担当する。

- C1 `TrackerObservation` / `ObservationDeviceState` loopback ingress
- source/session/device registry、retired session管理、pose freshness
- 永続 `(source_id, device_id) -> tracker_id` mapping
- coordinate profile解決
- normalization、shared world calibration、明示mount transform
- logical tracker cache
- output policy `steamvr / monaka / both / disabled`
- MTP `MtpPose` / `MtpTrackerState`生成
- SteamVR Direct向け共通thin driver/feed
- Utility Observation mirror
- 共通GUI/configuration、migration
- Task 2から移す旧PICO PC/SteamVR共通部のprovenance付き受け入れ

異種sourceのpriority/fallback/fusionを解決しない。PICOとVIVEは別Observationとして保持し、身体role/IK/fusionはTask 5 MonakaVRに残す。

## 非責務

以下をこのrepoへ入れない。

- PICO runtime/private ABI/POTB HMD decode
- VIVE HID/RF/vendor packet decode
- device-specific raw packet/type
- pairing/firmware/reset等のBackend操作
- body role / SteamVR roleの意味付け
- Fusion/Fallback/IK/constraint solver
- 未検証vendor battery/derivative推定
- Wi-Fi接続個数制限回避

vendor library/headerへの直接依存をBridge coreに持ち込まない。

## Dependency gate

`docs/codex/task4/00_upstream_handoff_status.md`のSHA/HEADは監査metadataであり、artifact bytesの代替ではない。

完了までに実物として必要:

1. Task 1 `monaka-protocol-kit-v1.0.zip` と外部handoff metadata
2. Task 2 `pico-bridge-extraction.zip` と外部manifest/report
3. Task 3 `vive-backend-handoff.zip` と外部manifest/report

Task 1正規値:

- kit SHA256 `eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729`
- source `5f41586b51bd84bb1cea344879b5d6325fc2c47d`
- schema `04f2d6c831c68a65edfbfb66ff8838d1c9d78535`
- protocol.lock SHA256 `234f0dffc46b808a179ec91da3c185794d7b7c83bc5b7d2bcb31fa73886bcb44`

kitはhash/internal `SHA256SUMS`/manifestを検証して `third_party/monaka-protocol/` に固定取り込み、provenanceを`dependencies/`へ保存する。独自JSON codecや互換風modelを作らない。CMake target `MonakaProtocol::Codec`を使用する。

Task 2/3 bundleが無い場合、空repo scaffolding、fixed codec integration、generic registry/mapping/calibration model、synthetic testsまでは進めてよい。しかしPICO code migration、VIVE fixture interoperability、Direct regression、cutover、Task 4 DoDを完了扱いにしない。

## 推奨module

- `src/observation/`: 29810 ingress、decode/validation、source/session/device registry、latest cache
- `src/mapping/`: persistent mapping、logical tracker ID、collision/revision管理
- `src/calibration/`: source profile、basis/quaternion mapping、rigid/world/mount transforms、derived angular velocity
- `src/routing/`: policy、MTP 29811、SteamVR internal feed 29812、Utility mirror 29813、bounded fan-out
- `outputs/steamvr/`: common thin provider/driver
- `tools/` / `ui/`: calibrator、tray/WPF、service control
- `config/`: profile、mapping、routing、migration
- `docs/`: provenance、migration、validation、hardware cutover

## 1. Task 2 extraction import

Task 2 extractionを最初に検証する。

- artifact hash、external manifest、source commit/path/blob SHA/SHA256を検証
- original notice/license/provenanceを保持
- extractionに含まれる旧SteamVR provider/driver、calibrator、C# UI、routing/IPC、build/scriptを起点にする
- PICO private runtime/ABI/Android codeを取り込まない
- 既存codeを確認せずSteamVR providerをゼロから書き直さない

移植先で大幅に変更したファイルにも由来を追跡できるmanifestを残す。

## 2. C1 Observation ingress

Bridgeだけが既定 `127.0.0.1:29810` をbindする。

- 1 datagram = 1 fixed C1 envelope
- max 4096 bytes、malformed/unsupportedをmessage単位で隔離・計数
- PICO/VIVEを`source_id`で分離
- 同時multi-sourceを許可
- 旧PICO receiverの「1 active HMD sender」制約をsource全体へ流用しない
- `session_id`変更は該当sourceだけcache/clock mapping/derivative historyを破棄
- retired sessionへの復帰を拒否
- one source restartで他sourceを消さない
- device_stateはpose freshnessを更新しない
- 初期pose timeout 500 ms設定可
- static numeric poseをstale判定しない

受信時に送信processのmonotonic epochを自clockと直接比較しない。C1規則どおり `age = sent_at_ns - timestamp_ns` を整数で求め、初回admission時のBridge monotonic clockへ固定写像する。通信遅延をclock syncと誤認しない。

## 3. Identity / mapping

永続mapping keyは `(source_id, device_id)`。

- slot、index、IP:portを永続IDにしない
- same source + same device collisionはfail closed
- 異なるsourceに同じdevice_id文字列があっても衝突させない
- logical `tracker_id`はconfig永続化
- SteamVR runtime serialはsource/tracker由来でcollision-freeにする
- 必要なら旧PICO serial migration tableを保持し、既存SteamVR role/configが不必要に失われないよう比較
- mapping/profile/calibration変更で`mapping_revision`を増やす
- body roleはmappingに含めない

## 4. Coordinate profiles / calibration

未知profileや未承認spaceをidentityでMTP/Directへ流さない。

### PICO compatibility profile

既存検証済み互換挙動:

- position: SI metresとしてそのまま
- orientation xyzw: `(x,y,z,w) -> (-z, y, -x, w)`

この二つを同一3x3 basisから機械的に導出したと仮定しない。profileではposition mappingとquaternion signed permutationを別表現できるようにする。これは既存PICO相対軸互換であり、絶対原点/完全な物理座標証明ではない。

### VIVE profile

`vut-native-v1`はTask 3で宣言済みだが、axes、device-to-space quaternionの物理解釈、1m scale、map共有はhardware NOT RUN。したがって自動identity承認しない。synthetic fixtureでprofile moduleを実装し、hardware検証済configだけoutput enableできる構造にする。

### Transform order

source convention → canonical pose → shared world transform → explicit device-to-mount transform。

Hamilton quaternionを使用する。

source→world `(R_WS,t_WS)`、device→mount offset `r_DM` / rotation `q_DM` の場合:

- `p_WM = R_WS * (p_SD + R(q_SD) * r_DM) + t_WS`
- `q_WM = q_WS * q_SD * q_DM`

lever armがnonzeroでvelocityを出す場合:

- `v_WM = R_WS * (v_SD + omega_S × (R(q_SD) * r_DM))`

必要なomegaが不明ならoffset先velocityはnull。accelerationも必要項不明ならnull。

旧PICO world translationをshared transformとして移す。per-tracker Position Zeroは診断でありplayspace calibrationとして保存しない。Orientation Zero既存式 `q_now * inverse(q_zero)` の診断互換を保持するが、通常MTPへ暗黙混入させない。

## 5. Normalization / derivative

同じnormalized/calibrated cacheをMTPとDirectへ供給する。片方だけ二重変換しない。

- valid quaternionはBridgeでnormalize
- q/-qを同回転として扱い、continuity処理をテスト
- unknown/raw vendor derivativeを継承しない
- Task 2の既存 `DeriveAngularVelocity` 相当を共通層へ移植可能
- `delta = q_now * inverse(q_prev)`、shortest arc
- dt 1..100 msのみ
- >100 rad/sはinvalid
- session/revision/zero/profile変更でhistory破棄
- MTPでunknown derivativeはnull
- SteamVR構造上必要な0を「測定0」としてMTPへ逆流させない

## 6. MTP output

固定C1 `MtpPose` / `MtpTrackerState`を生成し、既定 `127.0.0.1:29811` へ送る。

- conventionは必ず `rh_y_up_neg_z_forward`
- valid MTP quaternion norm `abs(norm-1) <= 1e-5`
- input provenanceに元`device_id/session_id/sequence`を保存
- Bridgeが新Observationを受理した時だけ通常MTP pose sequenceを進める
- calibration/profile更新による再出力はmapping_revision + MTP sequenceを更新可能だが元sample ageを保存
- transformed outputで保証可能なcapabilityだけを列挙
- confidence初期policyはfixed C1に従う

## 7. Routing / bounded fan-out

policy:

- `steamvr`
- `monaka`
- `both`
- `disabled`

旧alias `steamvr-direct` / `monaka-external`を一回migrationで読めるようにし、新namespaceへ書き戻す。元config backupを残す。

Observation mirror `29813` はoutput policyから独立。consumerごとにbounded latest slot/queueを持たせ、遅いGUI/Utility/consumerがingressを止めない。poseを無制限蓄積しない。

`both`はDirect trackerとTask 5 solver outputがSteamVR内で共存しうる。serialをdistinctにし、MonakaVRが自分のoutput/Direct mirrorを再入力しないidentity/config contractを文書化する。確認前に`both`をdefaultにしない。defaultは旧互換の`steamvr`。

## 8. Common SteamVR driver

SteamVR providerは既定 `127.0.0.1:29812` のMTP feedだけを受けるthin outputにする。

責務:

- tracker registration / dynamic lifecycle
- `DriverPose_t` conversion
- property update
- connection/validity submit
- local fixed ageからの`poseTimeOffset` clamp

責務外:

- PICO/VIVE profile mapping
- world calibration
- mount transform
- vendor packet parsing

既存dynamic lifecycleを維持する。vrserver lifetime中に登録済objectを安易に削除せず、stale/invalid時にinvalid/disconnectedをsubmitする。

partial poseはfull SteamVR pose validにしないが、Bridge内部のorientation metadataを捨てない。`poseTimeOffset`は別process clock同士を直接引かず、Bridgeで固定したlocal ageを使い既存 `[-100ms,+20ms]`範囲にclampする。

共通driverは1本だけ配布する。

## 9. GUI / config

GUIが扱うもの:

- source/device一覧
- logical tracker ID
- profile/calibration state
- common alignment
- output policy
- migration/status/health

扱わないもの:

- VIVE pair/RF/raw packet
- PICO firmware/private runtime操作
- device-specific reset/firmware機能

利用者にC ABI/UDP内部構造を直接操作させない。

## 10. Performance

C1 JSON ingressとfan-outについて実測する。

- CPU
- allocations
- ingress→egress p50/p95/p99 latency
- bounded queue behavior

将来binary protocolが必要でもTask 4で独自wireを追加しない。MonakaProtocol側の共通version更新として扱う。

## Compatibility / cutover order

旧PICO Direct/GUI baselineを新common driver/serviceで再現し、compatibility evidenceをTask 2 Phase Bへ渡してから旧path除去を許可する。Task 4自身はTask 2 repoを変更・削除しない。

推奨順序:

1. fixed protocol / synthetic scaffold
2. Task 2 extraction import
3. PICO mock/Direct regression
4. Task 3 fixture interop
5. PICO hardware compatibility
6. VIVE hardware/profile validation
7. dual-source coexistence
8. Task 2 Phase B handoff
9. Task 5 consumer integration

旧providerを先に消さない。

hardware未確認の場合は `implementation complete / hardware cutover pending` と明記する。

## Tests

### Unit

最低限:

- multi-source registry
- same device_id across different sources
- same-source collision
- session replacement / retired session
- state vs pose freshness
- null/validity semantics
- unknown profile rejection
- q / -q continuity
- PICO position/orientation profile fixture
- rigid/world/mount transforms
- lever arm behavior with known/unknown omega
- derivative dt/discontinuity/session reset
- mapping revision
- config migration/backup
- bounded queues

### Mock end-to-end

Task 2/3 fixturesを使用し:

`Observation -> ingress -> registry -> mapping/profile/calibration -> common cache -> MTP + Direct adapter`

を検証する。

さらに:

- invalid component
- duplicate/reorder
- different source clock epochs
- Bridge restart
- one source restart while other continues
- consumer stopped/reopened
- route changes
- disabled route + Utility mirror
- old/new double bind rejection
- same normalized sampleがMTP/Direct両pathで同じcalibration結果

### Build

- native C++17 service/driver: Windows x64
- OpenVR SDK path/version lock
- tray/WPF: Task 2 extractionの既存build方式を維持
- core generic modulesはnon-Windows mock build可能にする

実際のgenerator/toolchain/commandsを最終報告に記録する。

### Hardware

実行可能なら:

- PICO 5 trackers: position/orientation/velocity/zero/calibration
- VIVE Hub停止 + official dongle input
- PICO + VIVE coexistence
- SteamVR restart
- route switching
- same point/orientationでDirect vs MTP/Monaka input比較

未実施はNOT RUN。Task 3がVIVE hardware NOT RUNだった事実を上書きしない。

## Definition of Done

Task 4 completeには以下が必要。

1. fixed Task 1 kitがhash検証済みで使用される。
2. Task 2 extraction bundleがprovenance/hash検証付きで取り込まれる。
3. one common Observation ingressが複数sourceを独立sessionとして扱う。
4. persistent mapping/profile/calibrationがvendor依存なしで成立する。
5. PICO compatibility profileが旧fixture/regressionを維持する。
6. unverified VIVE native spaceをidentityとして自動出力しない。
7. one normalized/calibrated cacheからMTPとDirectを生成する。
8. one common SteamVR driverがthin MTP outputとして成立する。
9. routing/config migrationとbounded fan-out testsがある。
10. Task 2/3 fixture interoperabilityが確認される。
11. Task 2へPhase B用のMonakaBridge HEAD、artifact/manifest、compatibility resultsをhandoffできる。
12. build/mock/hardwareをPASS/FAIL/NOT RUNで区別する。

Task 2/3 artifact不在ならDoD completeと報告しない。hardware不在でもsoftware implementation completeは可能だが `hardware cutover pending` を付ける。

## Final report

以下を実値で埋める。

```text
repository: MonakaVR/MonakaBridge
audited_base_branch: main
audited_base_sha: 686392ead354292cab02422c942bf0236dee4255
actual_base_branch:
actual_base_sha:
base_change_reason:
work_branch: refactor/monaka-layer-separation (or Codex-managed work with verified ancestry)
HEAD_SHA:
protocol_version:
schema_commit:
protocol_kit_sha256:
contract_c1_sha256:

task2_handoff_source_commit:
task2_extraction_sha256:
task3_handoff_source_commit:
task3_handoff_sha256:
changed_files: path + reason
build_result:
unit_test_result:
mock_or_cross_language_result:
direct_regression_result:
hardware_validation:
compatibility_status:
performance_result: CPU / alloc / latency p50 p95 p99
phase_or_DoD_status:
unresolved_issues:
followup_required_in_task2:
followup_required_in_task5:
handoff_artifacts: filename / SHA256 / source commit
```

最後に以下を短くまとめる。

- Bridgeへ移した責務
- PICO Backendに残した責務
- VIVE Backendに残した責務
- MonakaVRへ残した責務
- 旧consumer/Direct pathを壊さずcutoverできる根拠
