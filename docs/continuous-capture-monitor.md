# N0b continuous monitor — 承認済み設計判断

2026-10-17。基点 `29245bd885ffd502df52a9a654887aed208d3bf2` は親の独立レビューで限定software受入済み。
先行提案のsourceScope periodic check/old-config pause/pre-replace abort案は採用しない。
本変更は単なる判定の機械抽出ではなく、単独capture ownerとSession workerの並行処理・所有権変更。

1. capture ownerは入力とreferenceを監視し、filesystem/helper hash/config read/parseを行わない。
   workerだけがdiscovery/confirmation/begin/result公開/prepare/precommit境界で既存exact scope検査を行う。
   solve中も入力ownerは独立して動く。scopeはboundary-basedで、検査間変更→復元は検出保証なし。
   periodic scope checker/schedulerや新しい観測protocolは作らない。
2. receive/delivery watermark、pose/state highwater/ring/freshness、invalidation epochは別状態。
   epochはsticky fault/scope/lifetime失効時のみ進む。通常sample・duplicate・成功scope検査で進めない。
   受信した全selected入力を同じ判定で検査した後だけguard snapshotへ縮約する。
3. bounded discoveryの受入集合からhandoff時にimmutable floorを確定し、counter/ringをmoveする。
   state floor Noneは固定。Sessionやrecording側で初期集合を再consumeしない。
4. recordingはowned canonical値・original時刻・その時点のeligibilityを保持する。
   pendingとin-flightを合算してsource128/reference128を上限とし、overflow/fault時はpublish禁止。
   stop ACKのreceive cutoff Nとdelivery ACKを分離し、Nまでの全配送ACK後だけarraysをcloseする。
   queue遅延を新しい時刻/鮮度で上書きしない。solve/review中もsticky faultをfreshで消さない。
5. precommitはworkerのartifact/token/readonly/TTL/exact scope検査→run/request付きowner入力barrier
   →workerが短いlock内で同run/同barrier/同invalidation epoch/faultなしと最新時刻の鮮度を確認
   →COMMITTING。通常sample進行はretry理由にしない。ACKは一回限り、cancel/stale/run違いを拒否。
   barrier待機は既存bounded deadlineを使い、待機/retryで期限を延長しない。
6. COMMITTING前のknown faultは拒否。移行後は既存B2 transactionを完了する。
   新しいpre-replace abortは作らず、保存確認ならSAVED_CONFIG_EVIDENCE_STALE、不確定ならUNKNOWN。
   config競合は既存exact CAS。保存とobserved lifetimeは非atomicで、再送/自動rollbackなし。
7. monitor mutex中は小さい状態更新/copyのみ。I/O/callback/solver/join/waitは禁止。
   `Session::execute`/`tick`/recorder/drafts/commitの単独ownerはworker。
   late callbackはrun generationと寿命を保持するcontextで隔離し、raw Session/recorderをcaptureしない。
   detach後もcontextはowner終了/in-flight完了まで保持し、lock外でjoinする。
8. cancel/EOFは既存latch、writerはworker終了後の既存2秒grace。owner/solverの停止不能は
   独立parent watchdog fixtureで証明する。U1製品client完成とは呼ばない。既存TTL/API/deadlineは維持。

scope: capture monitor typed state/snapshot、単独owner、bounded recording queue/tagged barrier/stop、
Session接続、最小idle失効通知、tests/CMake/docs/追加scope pin。
既存B2/N0a判定を一箇所で再利用し、canonical Frame/ProviderBatch/Envelopeを使う。
SDK/socket実adapter・GUI/package・drafts/solver/config serializer/transaction・Bridge wire/Core/backendsは変更しない。
production factoryはUnsupportedのまま。Standing matrix比較/toleranceはN1別review。

必須検証はhandoff前後conflict/None floor/二重counter防止、通常高rate/precommit、ACK後fault、
worker config read stall中のabsence/conflict、合算overflow/cutoff drain、solve/review sticky、
cancelled/stale barrier、old-generation callback、shutdown再入、owner stall、B2 commit window outcomeと
最終source上の既存30 CTest系・B1/transaction/math/全scope/old pins/negative controls。

mirrorはlatest-onlyで、未受信upstream historyを保証しない。受信済みfaultのsticky保証と区別する。
旧0xC0000409の個々のstackは未保存。共通995機序は旧race/deadline両modeで直接確認済み。
N1/U1/UI/HILは未成立。実環境設定・実機・SteamVR・実用UDP・GUI・dev/main mergeは対象外。
PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED

## 実装上の互換確認

入力判定はCaptureMonitor一箇所、Sessionはworker上で検査済みowned配送を記録する。
CaptureProvider.shutdownはdiscovery開始後のownerで一回呼び、実adapterはN1までUnsupportedのまま。
旧T03は新しいboundary-based scope契約に合わせ、idleでfilesystemを検査しないこととbegin前のhelper drift拒否を検査する。
旧501入力fixtureは値・synthetic時刻・期待結果を保ち、単独ownerへの適応としてmutex/atomicとtest-only高分解能1ms pacingを追加する。
高速・停止consumerの合算overflowは別の明示fixtureで検証し、製品timeoutやqueue capacityを緩めない。

## N0b independent-review corrections

- Owner clock ordering belongs only to successive owner samples. A worker freshness evaluation can overtake a clock value already captured by the owner outside the mutex; it evaluates lifetime/freshness against the latest known owner time without advancing that owner watermark. A true decrease between owner samples still causes sticky `MONOTONIC_CLOCK_ROLLBACK`; the Session worker retains its own rollback check.
- The confirmation's original 10-second absolute deadline covers scope checks, the input barrier and draft setup before recording. The barrier receives only the remaining confirmation/worker-lifetime budget. Begin rechecks that same deadline, cancellation and worker TTL after the barrier and after each synchronous draft operation, before starting the next operation or entering recording. Synchronous filesystem calls cannot be interrupted by this deadline: if one returns after expiry, an already-created draft can remain for inspection, but capture/solve/config write does not start. The expired run is terminal; no deadline reset or automatic retry occurs. Filesystem failure/indefinite blocking remains subject to the existing failure/watchdog model.
- Any rejected final precommit admission destroys the review token and returns a transiently stale `PREPARED` result to `RESULT_READY`. Fresh data cannot reuse the old token; explicit prepare may issue a new token over the identical frozen candidate, without another capture or solve. Sticky faults still invalidate the run. Once `COMMITTING` is entered the existing transaction/outcome rules apply unchanged.

Native review fixtures force both guard/callback clock interleavings, real owner rollback, confirmation expiry during scope/barrier/draft setup, cancellation/worker TTL after draft I/O, and ACK→transient stale→fresh→old-token rejection→explicit reprepare with identical candidate bytes. They use fake providers and local temporary draft/config files only; production capture remains unsupported.
