# CLAUDE.md — baratsuki-hikaku

日時 A と日時 B で、信号データの**バラつき具合に差があるか**を調べる Qt 6 GUI。
別プロジェクト `Correlation_plot` のコピーを「素材」にして作る。元プロジェクトとの関係は切る(§5)。

技術的な知見(ビルド環境、QCustomPlot、DB API の使い方、ハマりどころ)は `TECH_NOTES.md` を参照。
ここには**このアプリの方針**だけを書く。

---

## 1. 目的と要件

1. 日時 A と日時 B を指定して、それぞれのデータを DB から取得する。
   - 時間幅は A・B それぞれ、既定で**過去 1 時間**(日時を終端とする)。**GUI で変えられる**こと。
2. A と B のデータのグラフを**左右に並べて**表示する(左 = A、右 = B)。
3. バラつき具合の**差が大きい信号を知らせる**。

## 2. 設計方針(決めたこと)

### 2-1. 画面

- 上部の操作欄(実装済み): 日時 A(`QDateTimeEdit`)+ 時間幅 A、日時 B + 時間幅 B、`Get` ボタン。
  その下に、閾値(倍率)の `QDoubleSpinBox`、「Same Y range」と「Center」のチェック。
  時間幅は数値(`QSpinBox`)+ 単位(分/時間 など)。**既定は 60 分**。
- 本体: 信号ごとに 1 行。**4 列: A のトレンド | A の散布図 | B のトレンド | B の散布図**
  (散布図は §2-4)。信号が増えるので `QScrollArea` + `QGridLayout`
  (TECH_NOTES §3-2 の「スクロール領域に、動的に増えるグラフを並べる」を流用)。
- 同じ行の A と B は **縦軸の範囲をそろえる**(そろえないと、バラつきの大小が見た目で比べられない)。
  ここは、既定オン。切り替えられるとよい。
- 差が大きい信号の行は、色・印で目立たせ、一覧(表)にもまとめる(§2-3)。

### 2-2. データ取得

- **元の「ショットをタグ(sacla_tag / sr_tag)でそろえる」方式を引き継ぐ**(ユーザー確認済み)。
  複数の信号を同じショットでそろえて比べるため。取得は 2 段階:
  1. **タグの決定**(`Shot_data::get_tag`):
     基準信号 `sr_mon_screen_sep5u_otr/intensity` の各点が 1 ショット。各点の時刻で
     ルート信号 `xfel_tmg_sw_master_ema_route_inf_shm/current` を取り、`idata==769`(SR からの入射ショット)の
     `evno` を `sacla_tag` に。検索時間窓 `beg_time`〜`end_time`(`end_time` = ルート信号の先頭要素の時刻 + 2 秒)で
     `sr_safety_topup_intlk_beam_ssbt1/charge` を取り、最初に `fdata>0` となる点の `evno` を `sr_tag` に。
  2. **各信号の値の取得**(`db_get_sync_data`):
     信号を `db_add_signal_list` で登録(失敗した信号は除外)。ショットごとに `db_get_data_multi` で
     `beg_time`〜`end_time` の全信号をまとめて取得し、`evno` が `sacla_tag` または `sr_tag` に一致する点だけを
     `data[n].value[信号名]` に入れる。
- **時間範囲での取得には、元の `get_tag(begtime, endtime)` を使う**(基準信号を `db_get_data_f` で時間範囲取得)。
  「終了時刻以前の N 点」を取る `get_tag(endtime)` は、A/B では使わないので外した。
  A と B で別々の `Shot_data`(旧 `Corr_data`)を作り、それぞれ `get_tag(beg, end)` → `db_get_sync_data()`
  (`MainWindow::fetch_shots`)。`beg = 日時 − 時間幅`、`end = 日時`(日時は終端)。
- 元との違い(意図した変更):
  - route 信号に `idata==769` が見つからなかったショット(`sacla_tag==0`)は、検索時間窓も無く値が取れないので、
    `get_tag` の最後で**除く**(ターミナルに件数を出す)。元は空のショットが残っていた。
  - `Shot_data::value()` は、値が無いとき **NAN** を返す(元は 0 を返し、しかも `map::operator[]` でキーを作っていた)。
    バラつきの計算では、欠測を 0 と数えないため。使う側は `std::isfinite` で除くこと。
- 取得結果は必ず `free()` / 解放関数で解放(TECH_NOTES §3-6)。
- 取得は A と B で同じ信号リストを使う。片方に無い信号は、その旨を表示する。
- **注意(性能)**: 1 ショットごとに DB への問い合わせが数回走る。時間幅 1 時間のショット数は未確認で、
  多いと取得に時間がかかる。実機で件数と所要時間を測り、必要なら進捗表示・キャンセル・間引きを入れる。
  DB 取得中に GUI が固まらないようにすることも検討する(A と B の取得は独立)。
- タグのオフセット(`sacla_tag`/`sr_tag` に足す値)も、元のとおり残す。

### 2-3. バラつきの指標と「差が大きい」の判定

| 項目 | 方針(初期案。§4 で確認) |
|---|---|
| バラつきの指標 | 標準偏差 σ を基本に、相対値(σ / \|平均\|)と、外れ値に強い指標(MAD か IQR)も出す |
| 差の指標 | σ_B / σ_A(比)。対称にするため、**max(比, 1/比)** を「倍率」として並べる |
| 統計的な裏づけ | 等分散の検定(Brown–Forsythe / Levene。正規性を仮定しない)。データ数が少ないときは判定を保留 |
| 知らせ方 | 倍率が閾値以上(既定 1.5 倍)の信号を、一覧の上位に出し、行を強調。ステータスバーとウィンドウタイトルに件数を表示 |
| 閾値 | 画面の `Threshold` で変えられる。初期値と、有意水準・最小個数は `settings.ini` の `[Difference]` |

**実装済みの判定(`variation.h/.cpp`)**: 倍率 = max(σ_B/σ_A, σ_A/σ_B)。
- `LARGE`(赤): 倍率 ≥ 閾値、かつ Brown–Forsythe 検定の p < alpha(既定 0.05)
- `large (not significant)`(黄): 倍率 ≥ 閾値だが p ≥ alpha(個数が少ない、外れ値の影響など)
- `-`: 倍率 < 閾値
- `n/a`(灰): A・B のどちらかの個数が `min_shots`(既定 5)未満、または定数(σ=0)で比べられない
- 参考値として、四分位範囲の比(IQR B/A)と変動係数の比(CV B/A)も一覧に出す(判定には使わない)

- 平均が違うだけ(バラつきは同じ)の場合を「差」としないこと。**バラつきだけ**を比べる。
- 欠測・定数(σ = 0)の信号は、ゼロ除算にならないよう別扱いにして、一覧に「判定不可」と出す。
- ゆっくりしたドリフトを除いたバラつきを見たい場合に備え、`Shot_data::detrend()`(static)は残してある
  (オプションとして使う。既定はオフ)。

### 2-4. 基準信号と相関の散布図

- **基準信号 = 信号リスト(`signals.txt`)の一番上の信号**(ユーザー指定)。リストの先頭は太字で表示する。
  リストの信号を**ダブルクリックすると先頭に移る**(元のアプリと同じ操作。`signals.txt` にも保存される)。
  Get した時点のリストの先頭を使う(あとでリストを変えても、表示中の行はずれない)。
- 各トレンドグラフの右隣に、**散布図**を置く: 横軸 = その行の信号、縦軸 = 基準信号。同じショットどうしの組
  (どちらかが欠測のショットは除く)。A の散布図は A の期間、B の散布図は B の期間のデータ。
- 左上の注記: `n`(組の数)、`r`(ピアソンの積率相関)、`rho`(スピアマンの順位相関。同順位は平均順位)。
  r は直線的な関係、rho は単調な関係を見る(放物線の関係は両方とも約 0 になる)。個数が 3 未満、または定数なら `-`。
- 基準信号自身の行は、自分自身との散布図が無意味なので、空にして `reference signal` と表示する。
- 横軸の範囲・Center で引く量は、同じ行のトレンドの縦軸と**同じ**。縦軸(基準信号)の範囲と引く量は、
  基準信号の行のトレンドと同じ。Same Y range がオンなら、A と B の散布図も同じ範囲にそろえる。
- 背景色は、同じ行のトレンドと同じ(差の判定の色)。**相関そのものは、判定・一覧には使っていない**
  (相関の A/B の違いを知らせるかは、未決: §4-8)。

## 3. 元コードの扱い(「素材」として)— 固有ロジックを外し終えた(2026-11-01)

元のソースは `Correlation_plot` のもの。git が無いので、外す前のファイルは作業用の退避場所にだけ残してある
(このセッションのスクラッチパッド。消えうる)。元のロジックを見たいときは、元プロジェクトのコピーを参照すること。

### 現在のファイル構成

| ファイル | 内容 |
|---|---|
| `main.cpp` | 起動。`setApplicationName("baratsuki-hikaku")`、コマンドライン引数の処理(下の「コマンドライン引数」) |
| `period_args.h/.cpp` | 引数の日時・時間幅の文字列の解釈(`parse_period_time` / `parse_period_width`)。QtCore だけに依存 |
| `tests/test_period_args.cpp` | `period_args.cpp` の単体試験(実行方法はファイル先頭のコメント) |
| `mainwindow.h/.cpp/.ui` | 画面。操作欄(日時 A・B、時間幅、Now、Get、閾値、Y 軸をそろえる)、信号ツリー、信号リスト、信号ごとの左右のグラフ、差の一覧 |
| `variation.h/.cpp` | バラつきの指標(個数・平均・σ・CV・中央値・MAD・IQR)と、A/B の比較・判定・Brown–Forsythe 検定、相関(`pearson_r` / `spearman_rho`)。**Qt にも DB にも依存しない** |
| `tests/test_variation.cpp` | `variation.cpp` の単体試験(実行方法はファイル先頭のコメント) |
| `shot_data.h/.cpp` | データ取得層 `Shot_data`(旧 `Corr_data`)。タグ決定 `get_tag(beg,end)`、値の取得 `db_get_sync_data`、`value()`、`get_range()`、`detrend()`、`outfile()` |
| `qcustomplot.cpp/.h` | QCustomPlot 2.1.0(Qt 6 対応済み)。**触らない** |
| `figures.qrc` + `abcicon.png` | アイコン(PNG を埋め込み) |
| `baratsuki-hikaku.pro` | ビルド定義 |
| `m.sh` | `/users/kenichi/m.sh` へのシンボリックリンク。**そのまま残す** |

### 流用した部品(元の関数を、ほぼそのまま)

設定ディレクトリ(`signal_list_file` / `set_config_dir`)、`ini_string`、`load_colors`(キーは `Colors/a`, `Colors/b`)、
信号リストの読み書き(`load_signal_list` / `save_signal_list`。行の先頭の語だけを信号名にする)、
信号ツリー(`create_tree` / `load_group_list`)、`check_config_files` / `open_config_files`、`show_tag_offset`、
スクロール領域のグラフ行(`create_plot_area` / `fit_row_height` / `ensure_plot_rows` / `eventFilter` のホイール制御。
元の `create_extra_plots` 系を、A(左)・B(右)の 2 列にしたもの)。

### 外したもの

- MIC・相関(`mine.c/.h`、`cppmine.*`)、ランダムフォレスト(`forest.*`、Importance)、Spearman・検定、Stats
- 散布図(基準信号 × 他信号)、ランキング、Rank by メニュー、More Scatter Plots
- Highlight・Gravity・平均の差、最新ショットの平均・十字印・最新 3 点の線、ビン平均の曲線、Gradient
- 基準信号の考え方(リストの先頭が特別)、分母信号・効率(`signals_denominator.txt`、`sig_d`)
- 「Add」「Fix」「Calc」「Threshold」「Auto reload」と周期タイマー、`add_tag` / `add_db_get_sync_data` / `get_tag(endtime)`
  (A/B は固定の過去の範囲を 1 回取るだけなので、追加取得は要らない)
- 範囲選択(Trend 上の 2 本の縦線、`pushButton_3`)、トレンドのマウス追従
- 自動保存の CSV(`CSV_FILE` の固定パス)と、File → Save。`Shot_data::outfile()` は残してあるが、まだ使っていない
- 設定ファイルの `signals.txt` のフラグ(`mean_label`)、`settings.ini` の `[LatestMean]` `[CrossColor]` `[Highlight]`

### コマンドライン引数

```bash
baratsuki-hikaku [dir]                       # 設定ディレクトリ(位置引数)
baratsuki-hikaku -c DIR | --config-dir DIR   # 同じ。両方の指定や 2 重指定はエラー
baratsuki-hikaku -a "2026/11/01 09:00:00" --a-width 1h -b now --b-width 90m --get
```

| 引数 | 意味 | 指定しないとき |
|---|---|---|
| `-a, --a-time` | 期間 A の終端。`yyyy/MM/dd HH:mm[:ss]`、`yyyy-MM-dd HH:mm[:ss]`、`yyyy-MM-ddTHH:mm[:ss]`、`now`(ローカル時間) | 現在の 1 日前 |
| `--a-width` | 期間 A の時間幅。数値 + `m` / `h` / `d`(単位なしは分)。`90`、`30m`、`2h`、`7d`、`1.5h`(= 90 分) | 60 分 |
| `-b, --b-time` / `--b-width` | 期間 B(A と同じ形式) | 現在 / 60 分 |
| `-g, --get` | 起動直後に Get する | しない(画面で押す) |

- 読めない値(日付の誤り、0 以下、整数の分にならない、入力欄の上限 100000 を超える)は、画面を出さず、
  メッセージを出して終了コード 1 で終わる。
- 引数で指定した値は、画面の入力欄に入る(あとから画面で変えられる)。`--get` を付けなければ、DB は読まない。
- `m.sh` は引数を渡さないので、引数付きで起動するときは、ビルドした実行ファイルを直接呼ぶ。

### 画面の動き(実装済み。実機では未確認)

- `Get` → `fetch_shots`(A と B)→ `update_view`。`update_view` は DB を読まず、取得済みのデータから描き直す
  (閾値・Same Y を変えたときも、これだけ呼ぶ)。
- 信号ごとに 1 行、4 列(A のトレンド | A の散布図 | B のトレンド | B の散布図)。トレンドの点は(ショットの終了時刻, 値)。
  平均 ± σ の薄い帯と、左上に `n / mean / sd`。散布図は §2-4。
  判定が `LARGE` なら両方のグラフの背景が薄い赤、`large (not significant)` なら薄い黄。
- 横軸は、それぞれの期間(日時 − 時間幅 〜 日時)。縦軸は既定で A と B をそろえる。
- 「Center A and B (subtract each mean)」(既定オフ): オンにすると、A・B それぞれの**平均を引いて**表示し、
  どちらも縦軸の 0 を中心にそろえる(平均が違っても、バラつきの幅を同じ位置で見比べられる)。
  引くのは**表示だけ**(`draw_plot` の `offset`)。σ・倍率・p 値・判定・一覧は変わらない。注記の `mean=` は本当の値のまま。
  縦軸ラベルに `(- mean)` が付く。Same Y range と組み合わせると、同じ範囲で比べられる。
- 下の一覧は、差が大きい順(判定 → 倍率)。行のダブルクリックで、その信号のグラフへスクロール。
- Get した時点の信号リストと期間を覚えている(`view_signals`、`win_*`)ので、あとでリストを変えても行はずれない。

### 暫定のまま残したもの(§4 参照)

- 信号ツリーの既定グループ 14 個(`load_group_list`)。DB の信号カタログで、相関専用ではない
- 取得処理の電荷換算 `fdata × 1.60219e-10`(値が 1e8 超、または `sr_safety` を含む信号。`efficiency` を含む信号は除く)
- 日時 A の既定は B の 1 日前、B は現在(時間幅は A・B とも 60 分)
- 信号リストの既定は空(`signals.txt` が無ければ、ツリーから選ぶ)

### 検証の状況

- 元のコード、外したあとのコード、グラフ描画・判定を足したあとのコードとも、シャドウビルドで**コンパイルは通る**
  (`-Wall -Wextra`。出る警告は、元からある `qcustomplot.cpp` のものだけ)。
- `variation.cpp` は、疑似データで単体試験済み(同じ分布 → 倍率 ≈ 1、σ を 3 倍 → 倍率 ≈ 3 で `LARGE`、
  逆向きも同じ倍率、平均だけずらす → 倍率 ≈ 1、データ不足・定数は `n/a`、NAN は無視)。
  相関も同じ試験で確認済み(直線 → r, rho ≈ 1、放物線 → 両方 ≈ 0、単調な指数関数 → rho = 1 で r は小さい、
  符号が負なら −1、定数・2 組以下は NAN、同順位は手計算と一致)。
- `period_args.cpp` も単体試験済み。`main.cpp` の引数処理は、画面の無いスタブ(`MainWindow` を差し替え、
  `QCoreApplication` で動かす)に付けて、正常・エラーの各場合の動きを確かめた。本物の起動は未確認。
  Brown–Forsythe の p 値は、数値積分による独立計算と 8 桁一致した。ASan / UBSan でも問題なし。
- リンクは、この WSL に GUI 用ライブラリ(`QWidgetEXPlugin`、`MessageQueue`、`graphwidgetplugin`)が無いため失敗する
  (元のコードでも同じ)。このコードはそれらを使っていないので、3 つを除いて手でリンクすると、実行ファイルはできる。
- Qt の platform plugin(xcb / offscreen)が無く、DB も無いので、**画面の実行は未確認**。実機での確認が要る。

## 4. 未決事項(実装前にユーザーに確認する)

1. (解決)ショットのタグ付けは**必要**(ユーザー確認済み。§2-2)。
2. (暫定)単位換算 `×1.60219e-10` は取得処理の一部として**そのまま残す**。不要なら指摘してもらう。
3. **比較する信号**: どの信号を対象にするか(固定リスト、ツリーからの選択、両方)。
   既定の信号リストと、ツリーのグループ名はどうするか。
4. **バラつきの指標**: σ のほかに、相対値(CV)や MAD / IQR のどれを主にするか。「差が大きい」の既定の閾値(既定 1.5 倍に変更済み)。
5. **グラフの種類**: 時系列のみでよいか、ヒストグラムも並べるか。
6. (暫定)A と B の時間幅は**別々に**変えられる(数値 + 単位 min / hour / day)。同じにしたいだけなら、片方に合わせて入力する。
8. 散布図の相関(r / rho)の、A と B の違いを、一覧に出す・知らせるか(今は散布図の注記だけ)。
   基準信号を、リストの先頭ではなく別の方法(専用の選択など)で選ぶほうがよいか。
7. (暫定)日時は**ローカル時間**の文字列 `yyyy/MM/dd HH:mm:ss` を、そのまま DB API に渡す(元のアプリと同じ)。
   `db_unixtime_us_to_date` が返す時刻と、同じ時間帯で扱われているかは、実機で確認する。

## 5. 名前の置き換え(全て `Correlation_plot` → `baratsuki-hikaku`)

対象:

- `Correlation_plot.pro` → `baratsuki-hikaku.pro`(`m.sh` が `*.pro` を自動検出し、TARGET = ファイル名になる)
- `main.cpp`: `setApplicationName`、コメント、`--config-dir` の説明、`setApplicationDescription`
- `mainwindow.h` / `mainwindow.cpp` のコメントとダイアログのタイトル(設定ディレクトリ `~/.config/baratsuki-hikaku`)
- `mainwindow.ui` のウィンドウタイトル
- `MEMO.txt`: `./m.sh -b baratsuki-hikaku -u ...`
- `TECH_NOTES.md` の 4 か所(ユーザーの指示どおり置き換える。ただし本文は元プロジェクトの記述なので、外したコードの関数名が残る点に注意)
- `Makefile` は**生成物**(`qmake` が `.pro` から作る)。手で置換せず、削除して `qmake` で作り直す

注意:

- **ハイフンは C++ の識別子に使えない。** 置き換えてよいのは、文字列・コメント・ファイル名・`.pro` の中だけ。
  クラス名・関数名・マクロ名には使わない(今のコードには、識別子としての `Correlation_plot` は無い)。
- 設定ディレクトリが変わる(`~/.config/Correlation_plot` → `~/.config/baratsuki-hikaku`)。
  旧設定は引き継がない。必要なら手でコピーする(`signals.txt` など)。
- 環境変数 `CORR_DEBUG`(詳細ログ)は `BARATSUKI_DEBUG` に改名済み。クラス `Corr_data` は `Shot_data`(`shot_data.h/.cpp`)に改名済み。
- 実行ファイル名: `m.sh` が `baratsuki-hikaku` と `baratsuki-hikaku_USE_DATASTAX_*` を `rm -f` する。
  同名のソースを置かないこと。

## 6. 作業の進め方

1. **CLAUDE.md**(この文書)でユーザーと方針を合わせる(済)
2. §4 の未決事項は、初期案で進めながら、実装の都合で必要になった時点で確認する
3. 名前の置き換え(§5)とビルドの確認(済)
4. 固有ロジックを外す(§3)(済。コンパイルは通る。実機は未確認)
   (git は、ある程度できあがってから**ユーザーが** `git init` する。それまで git 操作はしない)
5. データ取得層(A / B の時間範囲取得)(`Shot_data` と `fetch_shots` まで済。実機で件数と所要時間を測る)
6. バラつきの指標と判定(済。`variation.cpp`、疑似データで単体試験済み)
7. 画面(左右のグラフ、一覧、強調)(済。コンパイルのみ。画面は未確認)
8. 実機(DB あり)で確認 ← 次。見るところ: 取得時間とショット数、グラフの見た目、時刻のずれ(§4-7)、
   判定の閾値が実際のデータで妥当か、Detrend(ゆっくりしたドリフトを除く)が要るか

## 7. ビルドと実行

```bash
export QTGUI_SETUP=/prj_pro/ubuntu22pd64/gui_setup
export PRJ2010_OUT=/2010/out/<版>/prj/ubuntu22pd64
export USE_DATASTAX=false
export PATH=/usr/local/qt6/6.4.3/gcc_64/bin:$PATH
qmake baratsuki-hikaku.pro && make -j8
```

または `./m.sh -b baratsuki-hikaku`(対話で debug ビルドの有無を聞かれる)。

- 開発用の WSL では、`qmake`・`g++`・`make` は入っているが、環境変数 `QTGUI_SETUP` / `PRJ2010_OUT` は未設定。
  DB API が無ければ、コンパイルまでは通せても、リンクと GUI の実行はできない(TECH_NOTES §2-1)。
  DB に依存しない部分(指標の計算など)は、DB API をスタブで差し替えて単体で確かめる(TECH_NOTES §5)。
- 警告は `-Wall -Wextra` で確かめ、無視しない。
- **ソースのディレクトリで `m.sh` などをビルドすると、生成物(`ui_*.h`、`moc_*.cpp`、`*.o`、`Makefile`)が残る。**
  別ディレクトリ(シャドウビルド)でビルドすると、`#include "ui_mainwindow.h"` が古いほうを拾って、
  `.ui` に足した部品が「無い」というエラーになる(TECH_NOTES §4-6)。シャドウビルドで確かめるときは、
  ソースだけを別の場所にコピーしてビルドする(ソース側の生成物は消さなくてよい)。

## 8. コード規約

- 周囲のコードに合わせる: 日本語コメント、インデント 4 スペース、`if(...){` の書き方。
- 設定ファイルの読み書きは、**必ず `signal_list_file()` を通す**(保存先を 1 か所で変えられるように)。
- `map::operator[]` で欠測を判定しない(`count()` / `find()` を使う。TECH_NOTES §4-4)。
- 範囲外の添字を返さない。初期化されない変数を作らない。
- 周期タイマーを使う場合は「データの版番号 + 設定の署名」で、変化がなければ何もしない(TECH_NOTES §3-4)。
- 生成物(`ui_*.h`、`moc_*.cpp`、`qrc_*.cpp`、`*.o`、`Makefile`)はコミットしない。

## 9. リポジトリの状況

- このディレクトリは `/users/kenichi`(ホーム)の git リポジトリの中にあるが、そちらの `.gitignore` に
  `q_other/` があり、**追跡されていない**。ここで `git commit` しても、記録されない。
- 元プロジェクトとの関係を切るため、独立したリポジトリにするなら、このディレクトリで `git init` する
  (ただし `m.sh` はブランチ一覧を表示するので、リポジトリが無くても動くが、あったほうがよい)。
  **ユーザーが、プログラムがある程度できあがった段階で自分で `git init` する**(確認済み)。それまで git 操作はしない。
- `m.sh` は `/users/kenichi/m.sh` へのシンボリックリンク。他のプロジェクトと共有なので、ここでは編集しない。
