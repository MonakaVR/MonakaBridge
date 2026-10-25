# PICO Motion Tracker 仮モデル・修正版 Rev3
作成: dot / 2026-10-16 / dotのクラウド環境のみ

## 今回の変更
最終の指定「ロゴ側5.5」に合わせ、外周の合わせ目をロゴ側5.5 : 底面側4.5の位置へ変更しました。
- 本体の厚み: 15.2 mm
- ロゴ面から溝中心まで: 8.36 mm
- 底面から溝中心まで: 6.84 mm
- 溝中心座標: Z = −0.76 mm（Rev2は−1.52 mm）

側面をより丸く見せるため、上下の外周エッジRを3.0 mmから4.5 mmへ拡大しました。
直径38.5 mm・厚み15.2 mmはそのままです。中央の直線部分は9.2 mmから6.2 mmとなり、上下の丸みの範囲が広がっています。

## 維持した内容
- USB-C開口・外側の逃げ・内部の舌とボタンの中心高さ: Z = −3.0 mm。
- PICO凹文字: 初版から180°回転した向き。位置・サイズ・深さ0.35 mmを維持。
- ボタン、LED、USB内部の舌のソリッドはRev2と同一形状・同一位置。
- USB開口の目安8.34 × 2.56 mm、外側の逃げ9.2 × 3.3 mm。
- 合わせ目の溝の幅・深さは維持。Rev3ではUSB外側の逃げと溝の間に約0.49 mmの公称高さ方向のすき間があります。

座標原点は本体中心、ロゴ面がZ = +7.6 mm、底面がZ = −7.6 mmです。

## ファイル
- PICO_Tracker_Detailed_APPROX_Rev3.FCStd: FreeCAD GUI保存版。開くならこちら。
- PICO_Tracker_Detailed_APPROX_Rev3_mm.step: mm単位・4ソリッド。
- PICO_Tracker_Detailed_APPROX_Rev3_mm.stl: mm単位の結合メッシュ。
- PICO_Tracker_Detailed_APPROX_Rev3_m.obj / .mtl / approx_white.png: メートル単位のSteamVR候補形式。3ファイルを同じフォルダーに置いてください。
- Housing_engraved_PICO.stl / Button_approx.stl / LED_lens_approx.stl / USB_C_tongue_approx.stl: 各部品のmm単位STL。
- FreeCAD_Rev3_perspective.jpg / FreeCAD_Rev3_side_profile.jpg: dotのクラウドFreeCADで実ファイルを開いた画面。
- build_details.py / text_polygons.json: 再生成ソース。
- PICO_Tracker_Base_R4p5_mm.step: 詳細加工前のR4.5 mmの外形。生成スクリプトで直接作るので再生成時の入力には不要です。
- export_steamvr.py: メッシュ変換ソース。
- details_validation.json / steamvr_mesh_validation.json / obj_readback.json: 検証結果。
- SHA256.json: 同梱ファイルのチェックサム。

FCStdのRevisionDimensions欄は寸法記録です。形状の再生成はbuild_details.py冒頭の定数を変更して実行してください。スケッチ拘束付きの完全な履歴モデルではありません。
生成にはFreeCAD Pythonモジュール、OBJ変換にはBlenderを使用します。パスは実行環境に合わせて変更してください。

## 検証
FreeCAD 1.0.0の実GUIで、丸みを増した外形・変更した溝・USB・ボタン・180°回転済み文字を確認。
FCStd/STEP再読込後も4個の有効なソリッド。
正確なSTEPのBRep境界: 約38.5000002 × 38.5000002 × 15.2000001 mm。
上下の丸みの曲面Rがともに4.5 mmであることを確認。
ボタン・LED・USB舌はRev2との差分体積ゼロを確認。
STL: 15,677頂点・31,338三角形。閉じたメッシュ。
OBJ: 1オブジェクト・1材質、UV/normal付き、メートル単位。
SteamVR実行試験は未実施。トラッキング原点・向きは前版の仮設定です。

## 精度と省略事項
画像と公称外寸に基づく推定モデルです。公式CAD、実機スキャン、製造図ではありません。
5.5:4.5とR4.5 mmはモデル内の設定値であり、実機の測定値ではありません。
PICO文字はDejaVu Sansを基にした普通の凹文字で、公式ロゴ書体・マークの再現ではありません。
USB-C認証済み製造モデルではなく、ケーブル挿入、筐体・マウントとの嵌合は保証しません。
電気接点、内部回路、ソケット、ストラップ、底面ラッチは未作成です。実機精度未検証の可視化・概略検討用途です。

## 前版から引き継いだ参考資料
公式外寸:
https://www.picoxr.com/global/products/pico-motion-tracker/specs
USB-C開口寸法の参考（PICO搭載部品と同一とは主張しません）:
https://cdn.amphenol-cs.com/media/wysiwyg/files/drawing/10168357.pdf
SteamVR形式の参考:
https://github.com/ValveSoftware/openvr/wiki/Driver-Render-Models
