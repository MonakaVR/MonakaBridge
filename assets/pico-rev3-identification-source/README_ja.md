# PICO Tracker Rev3 — 識別カラー版

既存の Rev3 SteamVR Resources OBJ を元に、形状を変えず色と UV のみを変更した別コピーです。元モデルは写真ベースの近似形状であり、実機の寸法・トラッキング原点の校正済みモデルではありません。

## 変更内容
- PICO の既存彫刻の底と内壁を黒で墨入れ。
- LED レンズを青で識別。発光表現ではなく、単一の色テクスチャです。
- USB-C の内部舌部をグレー、開口の内面・段差を濃いグレーに。
- 側面の既存の分割溝を濃いチャコールで墨入れ。溝の形状・深さ・幅は変更していません。
- ボタンは控えめなライトグレーで識別。
- 背面の平面に大きな黒い方向矢印を追加。矢印は LED とボタンのある側を指します。USB 側ではありません。

## 方向と寸法
OBJ の座標系・原点は元ファイルのままです。単位はメートル。
- ロゴ側の面: −Y。背面: +Y。
- LED・ボタン側: −Z。背面の矢印: −Z。
- USB 側: −X。
- 元 CAD の座標から OBJ への関係: (x, y, z) → (x, −z, y)。
- 公称外径 38.5 mm、厚み 15.2 mm、縁 R4.5 mm を保持。
- 溝中心の Y = +0.76 mm。ロゴ側から溝中心まで 8.36 mm、溝中心から背面まで 6.84 mm、比率 5.5 : 4.5 を保持。
- ロゴの 180 度回転、および USB・ボタンの CAD Z = −3 mm は既存 Rev3 のままです。

## ファイル
resources/rendermodels/pico_tracker_rev3/:
- pico_tracker_rev3.obj: 色分け用 UV に変更した本体。頂点・法線・面の並びは保持。
- pico_tracker_rev3.mtl: 1 マテリアルのみ。PNG を相対パスで参照。
- pico_rev3_identification_atlas.png: 2048×2048 の単一 RGB アトラス。
- color_validation.json: 色分け対象と方向の記録。

PICO_Tracker_Rev3_Identification.blend: 編集・確認用。テクスチャをパック済み。モデルに加えてプレビュー用カメラ・ライトが含まれます。SteamVR 用には resources 内の OBJ/MTL/PNG を使用してください。

renders/: 同じ照明・カメラでの変更前後の正面、背面、LED・ボタン側、USB 側、斜めの画像。front はロゴ面、back は背面です。低解像度の比較も付属します。これらは Blender での確認画像であり、SteamVR 実機画面ではありません。

original_reference/: 比較・再生成用の元 OBJ/MTL/PNG を無変更で同梱。

## 検証
- 15,677 頂点、31,338 三角形。
- 元ファイルとの比較で v / vn 行、および各面の頂点・法線インデックスと順序が完全一致。
- OBJ の再読み込み、PNG の読み込み、相対パスの参照、1 マテリアルを確認。
- 原点・軸・スケールを変更せず、追加のジオメトリ、切削、ポリゴン削減は行っていません。
- 元の CAD・STL は色を保持しないため編集していません。
- SteamVR の登録・インストール・サービス設定・Git 変更は行っていません。実行時互換性は未テストです。
- 溝は実寸約 0.2 mm、LED は約 0.96 mm と小さいため、極端に小さい表示では見えにくくなります。識別のために形状を太らせる変更はしていません。

## 再生成
Python 3 + numpy + Pillow、確認用レンダリングには Blender 4.3.2 を使用。

python scripts/colorize_rev3.py original_reference/pico_tracker_rev3.obj resources/rendermodels/pico_tracker_rev3
python scripts/validate_color_export.py original_reference/pico_tracker_rev3.obj resources/rendermodels/pico_tracker_rev3/pico_tracker_rev3.obj
blender -b -t 6 --python scripts/render_validate.py -- . original_reference/pico_tracker_rev3.obj

色の編集: colorize_rev3.py 内の colors、arrow を変更して再生成してください。背面の UV は X と −Z から平面投影し、その他はアトラス内の色スウォッチへ割り当てています。元のスマート UV 展開は保持していませんが、形状と法線は保持しています。
