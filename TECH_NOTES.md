# TECH_NOTES — 技術メモ(別アプリへの流用用)

Qt 6 + QCustomPlot + 社内 DB API で作った、信号データ可視化・解析 GUI から、
**アプリ固有の仕様を除いて**、再利用できる技術的な知見だけをまとめたもの。
関数名・ファイル名は、本リポジトリ(`devel3` / `master`)のもの。行番号は変わるので書かない。

---

## 1. 使っているライブラリ・API・バージョン

| 区分 | 内容 | メモ |
|---|---|---|
| OS / コンパイラ | Ubuntu 22.04(WSL2)、g++ 11.4.0 | `-std=gnu++1z`(C++17)。`.pro` に `CONFIG += c++11` と `c++17` の両方があるが、実効は c++17 |
| Qt | **6.4.3**(`/usr/local/qt6/6.4.3/gcc_64`) | モジュール: core gui widgets printsupport。ランタイムの Qt は別ビルド(`/home/qt/work/qt/qtbase`、xcb プラットフォームプラグイン)のことがある |
| ビルド | qmake(`baratsuki-hikaku.pro`) | 環境変数 `QTGUI_SETUP` の `.pri` を `include($$(QTGUI_SETUP)/...)` で読む |
| グラフ | **QCustomPlot 2.1.0**(`qcustomplot.cpp/.h` を同梱) | Qt 6 の moc に合わせて **ヘッダを修正済み**(§4-1) |
| 相関指標 | **MINE**(`mine.c` / `mine.h` = libmine 1.2.5、C++ ラッパー `cppmine.cpp/.h`) | MIC(`EST_MIC_APPROX`)を使用 |
| 機械学習 | 自前のランダムフォレスト(`forest.cpp/.h`、依存なし) | OOB permutation importance |
| DB | 社内 DB API(`db_common.h`、Thrift 版 `mdaq_db_thrift`) | リンクは `.pri` 経由。ヘッダは `/2010/out/<版>/prj/ubuntu22pd64/include` 以下 |
| その他 | `boost/format.hpp`(ヘッダのみ。実質未使用)、`<random>`、`<regex>`(避けた) | |
| 設定 | `QSettings`(INI)、`QCommandLineParser`、`QStandardPaths`(AppConfigLocation) | §3-1 |

---

## 2. ビルド・実行環境の注意点

### 2-1. 環境変数とヘッダの場所

```bash
export QTGUI_SETUP=/prj_pro/ubuntu22pd64/gui_setup           # .pri の場所
export PRJ2010_OUT=/2010/out/2026.0708.0827/prj/ubuntu22pd64 # DB API のヘッダ・ライブラリの版
export USE_DATASTAX=false                                    # false = Thrift 版の DB API
export PATH=/usr/local/qt6/6.4.3/gcc_64/bin:$PATH
qmake baratsuki-hikaku.pro && make -j8
```

- `.pri` は、`LIBS` に `-lQWidgetEXPlugin -lMessageQueue -lgraphwidgetplugin` なども一括で足す。
  **コードが使っていなくても**、これらのライブラリが無い環境ではリンクで失敗する。
- DB API のヘッダ(`db_common.h` など)は、`/prj_pro` ではなく `/2010/out/<版>/…/include` にある。
  版(`2026.0708.0827` など)は更新で変わるので、`PRJ2010_OUT` で指す。
- 実機以外(サンドボックスなど)では、**コンパイルまでは通せるが、リンクと実行はできない**。
  DB API の呼び出しを代用関数で差し替えれば、DB に依存しない部分は単体で動かせる(§5)。

### 2-2. 生成物とビルドの落とし穴

- ソースディレクトリで `qmake` / `make` した生成物(`ui_*.h`、`moc_*.cpp`、`qrc_*.cpp`、`*.o`、`Makefile`)が残る。
  別ディレクトリ(シャドウビルド)でビルドすると、`#include "ui_mainwindow.h"` が、
  **ソース側の古い `ui_mainwindow.h`** を先に拾って、`ui->checkBox_xxx` が無いというエラーになる(§4-6)。
  `.ui` を変えたら `make distclean` → `qmake` → `make`。
- `.gitignore` に `moc_*.cpp`、`ui_*.h`、`qrc_*.cpp`、`*.o`、`Makefile*`、実行ファイル名(環境で変わる)を入れる。
  実行ファイル名は `baratsuki-hikaku_USE_DATASTAX_false` のように、環境で変わることがある。
- デバッグビルド: `qmake CONFIG+=debug && make clean && make`(行番号付きのバックトレースが取れる)。
- WSL では、ダウンロードしたファイルに `xxx:Zone.Identifier` という付随ファイルができる。コミットしない。

### 2-3. 実行時のプラグイン

- **JPEG は Qt のプラグイン(`libqjpeg`)が無いと読めない**。環境によって、`QImage("a.jpg").isNull()` になる。
  アイコンなどは **PNG**(Qt に組み込み)にして、`.qrc` で実行ファイルに埋め込むのが確実(§4-12)。
- GUI の起動には、プラットフォームプラグイン(xcb)が要る。プラグインの無い環境では、GUI の実行テストができない。

### 2-4. 設定ファイルの置き場所

- 既定は `~/.config/<アプリ名>/`。**`QCoreApplication::setApplicationName()` を `MainWindow` を作る前に呼ぶ**こと
  (呼ばないと、実行ファイル名が、ディレクトリ名になる。実行ファイル名は環境で変わる)。
- 実行時の引数でディレクトリを変えられるようにした(§3-1)。

---

## 3. 実装パターン(どのファイルのどの部分が肝か)

### 3-1. 設定(ファイル・引数・色)

| 部品 | 場所 | 要点 |
|---|---|---|
| 設定ディレクトリの解決 | `mainwindow.cpp` の `signal_list_file()` と、`g_config_dir`(ファイル静的変数)、`MainWindow::set_config_dir()` | 引数があればそれ、なければ `QStandardPaths::AppConfigLocation`。**すべての設定ファイルの読み書きが、この関数を通る**ようにしておくと、保存先の変更が 1 か所で済む |
| コマンドライン | `main.cpp`(`QCommandLineParser`) | 位置引数 `[dir]` と `-c/--config-dir` の両方。2 重指定はエラー。相対パスは `QDir(dir).absolutePath()` で絶対パスに |
| INI の読み取り | `mainwindow.cpp` の `ini_string()` | **コメント除去とリスト対策を入れた文字列取得**(§4-8)。`load_colors()`、`latest_mean_shots()`、`init_highlight_threshold()` が使う |
| 色の設定 | `load_colors()`、ファイル先頭の `static QColor` 群 | `QColor(QString)` が `#RRGGBB` と色名の両方を読む。`isValid()` で検証し、不正なら警告して既定のまま。**グラフを作る前**(`plot_init` より前)に読む |
| 更新日時での再読み込み | `latest_mean_shots()`(`QFileInfo::lastModified()` と比較) | 描画のたびに呼んでも軽い。再起動なしで設定が反映される |
| 行ごとのフラグ付きリスト | `load_signal_list()` / `save_signal_list()`(`FLAGS_ROLE = Qt::UserRole+1`) | `名前  flag1 flag2` 形式。フラグは `QListWidgetItem::setData()` に持たせ、**自動保存のときに書き戻す**(保存で落とさない) |
| 設定ファイルの有無の警告 | `check_config_files()` | 起動後に `QTimer::singleShot(0, ...)` で呼ぶ。`QMessageBox::open()` で操作を止めずに出し、**親ウィンドウの中心に `move()`** する(WM 任せだと遠くに出る) |

### 3-2. GUI の構成パターン

- **設定の切り替えはメニューの `QAction`(checkable)**にまとめる(`createActions()`)。
  - 状態を**コンストラクタの早い段階から参照**する設定は、`QAction` を先に作る(メニューへの追加は後でよい)。
  - 排他選択は `QActionGroup`(`setExclusive(true)`)。`triggered` はユーザー操作でだけ出る
    (`setChecked()` では出ない)ので、プログラムから切り替えたら、**対応する処理を自分で呼ぶ**。
- **子パネル**は、`QDialog`(`Qt::WA_DeleteOnClose`)をコードで組み立てる(`show_importance()`、`show_tag_offset()`)。
  `QTableWidget` は `setSortingEnabled(true)` の**直後に**、`sortByColumn()` で並び順を明示する
  (有効にした直後は、既定の列の降順になる)。数値列は `setData(Qt::DisplayRole, double)` で数値ソートに。
- **右クリックメニュー**: `setContextMenuPolicy(Qt::CustomContextMenu)` と `customContextMenuRequested`。
- **外部コマンド**(エディタを開く): `QProcess::startDetached(program, args)`。戻り値で失敗を検出。
- **スクロール領域に、動的に増えるグラフを並べる**(`create_extra_plots()` / `ensure_extra_plots()` /
  `fit_extra_row_height()` / `set_extra_plots()`):
  - `QScrollArea`(`setWidgetResizable(true)`)の中に、`QGridLayout` を持つ `QWidget`。必要な数だけ、後から `QCustomPlot` を足す。
  - 行の高さは、`viewport()->height()` の 1/3 を `setFixedHeight()` する。`viewport()` に `installEventFilter()` して
    `QEvent::Resize` で更新。**他のウィジェットの高さを測って合わせる方式は、レイアウトが循環して崩れる**(§4-9)。
  - 広げる/戻すときは `resize()`。最大化・全画面のときはしない。`screen()->availableGeometry()` で頭打ちに。
  - ホイールは、グラフの拡大縮小に取られるので、`eventFilter` で、Ctrl なしなら、スクロールバーへ回す。

### 3-3. QCustomPlot のパターン(`mainwindow.cpp`)

| やりたいこと | 方法 | 場所 |
|---|---|---|
| 点ごとに色を変える | **色ごとのグラフ**(段階数ぶん)に点を振り分ける。1 点ずつの色指定は無い | `apply_display_impl()`(グラデーション) |
| 時間順に点を線でつなぐ | `QCPGraph` は x でソートされる。**順序を保つ `QCPCurve`**(パラメータ `t` を 0,1,2…)を使う | `update_recent_line()` |
| 縦線 / 帯 | `QCPItemStraightLine`(x は plot 座標、2 点の y を 0 と 1)。帯は `QCPItemRect` で、y を `ptAxisRectRatio`、x を `ptPlotCoords` に | `update_mean_lines()` |
| 描画の重なり順 | **後から作った物ほど手前**。常に手前に出したい物は `setLayer("overlay")`、下に敷きたい物は `setLayer("grid")` | `set_recent_points()`、`update_mean_lines()` |
| 点の形だけ変える | `QCPScatterStyle st = g->scatterStyle(); st.setShape(...); g->setScatterStyle(st);`(色・サイズ・塗りは保たれる) | `apply_shape()` |
| 平均 ± 誤差の折れ線 | `QCPGraph` + `QCPErrorBars`(`setDataPlottable`) | `update_binned_curve()` |
| 文字の注記 | `QCPItemText`(`ptAxisRectRatio` で軸の矩形内の割合の位置)。**1 つ作って使い回す**(§4-7) | `set_info_text()` |
| 再描画の負荷 | `replot(QCustomPlot::rpQueuedReplot)` で、重なった再描画をまとめる | `plot_data()` ほか |
| 非表示のグラフ | データを入れない(`setData` は x でソートするので重い)。**再表示のときに、保持しておいたデータを入れ直す** | `apply_display_impl()` |
| 別グラフの点と対応づける | 各点に**元のショット番号**(昇順)を持たせ、`std::lower_bound` で探す。表示から外れた点は出ない | `ScatterData::shot`、`highlight_shot()` |
| マウスで指した点を探す | 各点を `coordToPixel()` でピクセルに変えて、距離(例: 12px 以内)で最近傍を取る | `trendMouseMoved()` |
| マウスが出たとき | 対象ウィジェットに `installEventFilter(this)` して `QEvent::Leave` | `eventFilter()` |

### 3-4. 再計算の抑制パターン(`mainwindow.cpp` の `update_data()`)

周期タイマーで毎回、計算と描画をすると、変化がなくても重い。

1. データ側に**版番号**(`Corr_data::ver`)を持たせ、データが変わったとき(追加・削除・新しい値の格納)だけ増やす。
2. 結果に影響する**設定を 1 つの文字列にまとめる**(`calc_signature()`:チェック状態・入力欄・リスト内容など)。
3. 計算と描画を済ませたら、版番号と署名を記録(`mark_calculated()`)。
4. 次の周期で、「新しいデータなし」かつ「版番号が同じ」かつ「署名が同じ」なら、早期 return。

併せて入れた軽量化:

- `value(const string&, int)` は、信号名を**const 参照**で受ける(値渡しだと、要素ごとにコピーが走る)。
- 最小・最大は **1 回の走査**にまとめる(`get_range()`)。
- 複数の信号で共通の列(基準側)は**1 回だけ作る**(`calc_mic_rows()`)。
- DB の問い合わせは、**足りない値があるショットだけ**に絞る(先に `map::count` で走査。`add_db_get_sync_data()`)。
- 詳細ログは、環境変数でオンにするマクロ(`corr_data.h` の `DLOG`)。**オフのときは、引数の式も評価しない**:

  ```cpp
  inline bool corr_debug(){ static const bool d = getenv("BARATSUKI_DEBUG")!=NULL; return d; }
  #define DLOG if(!corr_debug()){}else cerr
  ```

### 3-5. 解析・統計(`corr_data.cpp` / `forest.cpp`)

| 部品 | 場所 | 要点 |
|---|---|---|
| MIC | `calc_mic_rows()`、`mic_alpha()` | データ数 n で `alpha` を段階的に(n ≥ 2500 で 0.55 … n < 25 で 0.85)。`MINE` は x, y を**書き換えない**(複数の信号で x を共有できる) |
| MIC の「偶然の範囲」 | `mic_null95()` | 無相関の一様乱数でのMICの 95 パーセンタイル。**値は n だけで決まる**ので、丸めた n ごとにキャッシュ(約 60 回の試行)。実測: 1 回の MIC は n=50 で約 2 ms、100 で 7.5 ms、300 で 37 ms、1000 で 72 ms |
| 順位相関と検定 | `calc_stats()`、`rank_avg()` | Spearman(同順位は平均順位)+ **並べ替え検定**(1000 回シャッフル)。固定シード(`mt19937(12345)`)で、表示が揺れない |
| ドリフト除去 | `Corr_data::detrend()` | 中心移動平均(端は窓を狭める)を引く。窓幅 = n/10 を 5〜25 に。累積和で O(n) |
| ランダムフォレスト | `forest.cpp`(`forest_importance()`) | 回帰木(葉の最小サイズ 3、深さ 14、`mtry = p/3`)、ブートストラップ、**OOB の permutation importance**(÷ yの分散)と OOB R²。実測: n=150, p=10 で約 20 ms、n=1000, p=25 で約 0.5 s。似た変数があると、重要度は分け合う |
| 2 群の差の指標 | `update_gap()` | 平均の差 ÷ 全体の標準偏差(単位に依存しないように) |
| 閾値の決め方 | `compute_highlight_threshold()` | パーセンタイル(線形補間)/ 平均+kσ / 固定値。**裾の長い分布では、平均+σ は基準が上限を超えうる**ので、パーセンタイルが無難 |
| 重み付き代表値 | `group_stat()`、`recent_weight()` | 重心(重み付き平均)、指数の重み(`0.5^(k/半減期)`)。重みが 0 以下のときは、平均に戻す |

### 3-6. DB API の使い方(`corr_data.cpp`)

- **データ点の構造**: `Db_f_data{ float fdata; int64 evno; int32 err, tdata, tmdata; int64 unixtime_us; }`(`Db_i_data` も同じ並び)。
  同期信号の値は、**`evno`(イベント番号)で、ショットにそろえる**。
- **取得系**:
  - `db_get_data_f/i(sig, beg, end, &arr, &cnt)`: 時間範囲。
  - `db_get_data_num_f/i(sig, end, num, &arr, &cnt)`: **`end` 以前の最新 `num` 件。新しい順**(`arr[0]` が最新)。件数が足りなければ少なく返る。
  - 複数の信号をまとめて: `db_init_signal_list` → `db_add_signal_list`(信号ごと)→ `db_get_data_multi(&list, beg, end, ...)` → `list.signal_list[j].data`(`Db_f_data*`)と `num_data` → `db_release_signal_list_all`。
- **メモリの所有**: `db_get_data_*` が返す配列は、呼び出し側が **`free()`**。
  `db_init_signal_list` は何も確保しない(`signal_list=NULL`)ので、信号を足さなければ、解放は不要
  (ライブラリを逆アセンブルして確認した。ソースが無いときの調べ方としても使える:`ar x` → `objdump -d -C`)。
- `db_add_signal_list` が失敗する信号(存在しない名前)は、リストから外す。
- 時刻の変換: `db_unixtime_us_to_date()` / `db_unixtime_us_add_sec()`。
- DB の取得結果の **件数が要求より多い**場合に備え、配列の添字は `data.size()` で頭打ちにする。

### 3-7. データ構造の落とし穴を避ける作り

- ショットごとの値は `map<string,double>`。**`operator[]` は、無いキーを 0 で作る**(§4-4)。
- 「ランキング → 信号の添字」を返す関数は、**必ず範囲内の値を返す**(§4-5)。

---

## 4. ハマった点と解決策

| # | 症状 | 原因 | 解決 |
|---|---|---|---|
| 1 | Qt 6 で `moc_qcustomplot.cpp: template argument 1 is invalid ... TypeAndForceComplete<QCP, std::true_type>` | QCustomPlot 2.1.0 は、`QCP` 名前空間を、moc のときだけ `class QCP{Q_GADGET}` に見せる古い方法。Qt 6 の moc は、これを型として参照する | `qcustomplot.h` で、Qt ≥ 5.8 は `namespace QCP { Q_NAMESPACE ... Q_ENUM_NS(...) / Q_FLAG_NS(...) }` に。`extern const QMetaObject staticMetaObject;` は不要(Qt ≥ 5.8 側では外す)。または QCustomPlot 2.1.1 以降を使う |
| 2 | `QTextStream` / `QDebug` に `endl` を流すとエラー(`<unresolved overloaded function type>`) | ヘッダに `using namespace std;` があり、`endl` が `std::endl` になる。Qt 6 は `Qt::endl` が必要 | `Qt::endl` と書く |
| 3 | CSV 書き出しが、行数に比べて遅い | 1 行ごとに `regex(" ")` を作って `regex_replace` していた | `std::replace`(約 30 倍速い。ただし 2000 回で 1.6 ms と、絶対値は小さい。重い処理は、MIC や DB 取得のほうだった) |
| 4 | 値が 0 のまま残る / 欠測の判定が崩れる | `map::operator[]` が、**読むだけでも**キーを 0 で作る。後から本物の値が届いても、「取得済み」と見なされて上書きされない | 判定は `count()` / `find()`。やむを得ず `operator[]` を使い続けるなら、この副作用を、設計として意識する |
| 5 | `Fix` などで保存済みの順位を使うと、**時々**セグメンテーション違反 | `int ret;` が**初期化されず**、該当する順位が無いと、ゴミの添字を返した(`-Wmaybe-uninitialized` の警告が出ていた) | 0 で初期化し、返す前に範囲内に収める。**コンパイル警告(`-Wall -Wextra`)を無視しない** |
| 6 | シャドウビルドで `no member named 'checkBox_xxx'` | ソース側に残った古い `ui_*.h` が先に見つかる(`#include "..."` は、まず、そのファイルのあるディレクトリを探す) | ソース側の生成物を消す(`make distclean`)、または、ソースをコピーしてビルド |
| 7 | 注釈の文字が、更新のたびに重なって読めなくなる / メモリが増える | 描画のたびに `new QCPItemText` していた | アイテムを 1 つ作って、`setText()` で使い回す(`set_info_text()`) |
| 8 | INI の値が空になる / リストになる | `QSettings`(INI)は、**行の途中のコメントを扱えず**、**カンマを含む値を `QStringList` にする**。また、色の `#00696B` の `#` を、コメントと区別したい | `ini_string()`: `QStringList` はカンマで連結し直す。コメントは、`;` と、**空白の直後の `#`**(`(;\|\s#)`)から後ろを取り除く(`#00696B` は残る) |
| 9 | スクロール領域内のグラフの高さが、上段より小さい | 最小サイズ固定のままで、上段は、レイアウトで伸びる。広げる前に上段の高さを測ると、タイミングやウィンドウの大きさで、ずれる(循環) | レイアウトの比率(上段の各段 : 領域 = 1 : 3)で、領域の高さを決め、**`viewport()` の高さの 1/3 を、各グラフの高さ**にする |
| 10 | スクロール領域の上でホイールを回すと、グラフが拡大縮小して、スクロールしない | `QCustomPlot` が、ホイールを受ける | `eventFilter` で、Ctrl なしのホイールを、スクロールバーの値に回して `return true` |
| 11 | 強調した点が、後から作ったグラフの下に隠れ、**白抜き**に見える | 後から作ったグラフが手前に描かれる(オンにしたときに初めて作られるため) | 常に手前に出す物は `setLayer("overlay")`。順序は、移した順(後ほど手前) |
| 12 | アイコンが出ない(JPEG) | JPEG のプラグインが無い環境がある | PNG を `.qrc` で埋め込む。JPEG を使うなら、`QImage::isNull()` で調べて、PNG に切り替える |
| 13 | `on_xxx_yyy` という名前の自作スロットに、`connectSlotsByName` の警告 | `on_` で始まるスロットは、自動接続の対象と見なされる | 手でつなぐスロットは、`on_` を付けない名前にする |
| 14 | `Qt::CustomContextMenuRequested` でコンパイルエラー | 列挙子の名前は `Qt::CustomContextMenu`(`Requested` は、シグナル名 `customContextMenuRequested`) | 列挙子とシグナルを区別する |
| 15 | 設定の保存先が実行ファイル名に依存する | `QStandardPaths::AppConfigLocation` は、アプリ名から決まる | `main()` で、`MainWindow` より前に `setApplicationName()` |
| 16 | `QActionGroup` で、プログラムから選んでも処理が動かない | `triggered` は、ユーザー操作でだけ出る | `setChecked()` のあとに、処理を直接呼ぶ |
| 17 | (潜在。実際には発生していない)先頭の項目に特別な意味があるリストで、**空になると**落ちうる | `item(0)->setFont()` など、先頭の存在を仮定していたコードがある | 先頭を触る前に、空でないことを確かめる |
| 18 | GCC 11 + `_GLIBCXX_ASSERTIONS` で、範囲外アクセスのメッセージが出ず、突然 abort | アサーション失敗のメッセージが `stdout`(バッファあり)に出て、abort で捨てられる | テストの先頭で `setvbuf(stdout, NULL, _IONBF, 0)` |
| 19 | (予防。実際の不具合ではない)データ取得の前に、周期タイマーが動いて、空のデータを計算・描画してしまう | 起動時の自動取得をやめたため、タイマーが先に動きうる | 「最初の取得が済むまで、自動更新は何もしない」判定(`update_data()` の `has_calculated`) |

---

## 5. テスト・デバッグの方法(GUI も DB も無い環境で)

1. **DB に依存する部分は、代用関数で差し替える**。`extern "C"` で `db_init_signal_list`、`db_get_data_multi` などを、
   乱数でデータを返すスタブとして定義し、`corr_data.cpp` と一緒にコンパイルする。
   リンクできない未解決シンボルは `-Wl,--unresolved-symbols=ignore-all` で逃がしてもよい(呼ばない関数のみ)。
2. **private メンバーを触りたいとき**は、テスト側で
   `#define private public` → `#include "corr_data.h"` → `#undef private`。
3. **サニタイザ**: `g++ -O1 -g -fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS`。
   乱数で操作の順序を変えて(取得・追加・削除・並び替え・全順位・全添字を読む)、何十回も回す。
   `get_ranking` の添字バグは、これで再現できた。
4. **旧版と新版の出力の比較**: リファクタリング(重複した処理の共通化など)のときは、
   `git show HEAD:file` で旧版を取り出し、同じ疑似データで両方を動かして、`diff` で完全一致を確かめる。
5. **統計の部分は、答えの分かる疑似データで確かめる**(線形・無相関・放物線・ドリフトのみ、など)。
   例: 放物線では MIC は高く、順位相関はほぼ 0 になる ── 「MIC だけでは符号が分からない」ことの確認にもなる。
6. **単体で確かめられる Qt の処理**(`QSettings`、`QCommandLineParser`、`QRegularExpression`、`QColor`)は、
   `QCoreApplication` だけの小さなプログラムで確かめる(GUI のプラットフォームプラグインは不要)。
   ただし、`QAction` や `QWidget` を作るものは、`QGuiApplication` / `QApplication` が要る(プラグインが無いと動かない)。
7. 実機でクラッシュしたら: デバッグビルドにして、`gdb` で `bt`。**そのときの操作・チェック状態・信号数**を、記録しておく。
8. 環境によっては `gdb` / `valgrind` が無い。その場合は、サニタイザと、アサーションで代用する。

---

## 6. 流用するときのチェックリスト

- [ ] Qt 6 で QCustomPlot を使うなら、2.1.1 以降か、`Q_NAMESPACE` の修正(§4-1)。
- [ ] ヘッダの `using namespace std;` と `Qt::endl`(§4-2)。
- [ ] 設定ファイルは、1 つの関数(`signal_list_file`)を通して読み書きし、保存先を引数で変えられるように。
- [ ] `QSettings`(INI)の読み取りは、コメントとカンマの対策(`ini_string`)を通す。
- [ ] 周期タイマーの処理には、「データの版番号 + 設定の署名」で、変化がないときのスキップを入れる。
- [ ] 範囲外の添字を返す関数、初期化されない変数がないか、`-Wall -Wextra` と、サニタイザで確かめる。
- [ ] 文字列を要素ごとに値渡し、`map::operator[]` で欠測の判定、描画のたびの `new` などを避ける。
- [ ] アイコン・画像は PNG で、`.qrc` に埋め込む。
- [ ] 生成物(`ui_*.h` など)を `.gitignore` に入れ、`.ui` を変えたら `make distclean`。
- [ ] DB の取得結果は、必ず `free()`(または、API の解放関数)。件数は、配列の大きさで頭打ちに。
