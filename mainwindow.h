#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QRegularExpression>
#include <QDateTime>
#include <QTableWidget>
#include "shot_data.h"
#include "variation.h"
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void create_tree();
    static void set_config_dir(const QString &dir);   // 設定ファイルのディレクトリ(既定は ~/.config/baratsuki-hikaku)を変える
    // 起動時の日時と時間幅(コマンドライン引数)。end が無効、width が 0 のものは、指定なし(画面の既定のまま)
    struct InitialPeriod{
        QDateTime end;          // 期間の終端
        int width = 0;          // 時間幅の数値
        qint64 unit_sec = 0;    // 時間幅の単位(秒。60 = 分、3600 = 時間、86400 = 日)
    };
    static void set_initial_period(bool is_b,const InitialPeriod &p);   // is_b=false が A、true が B
    static void set_auto_get(bool on);                // 起動直後に Get する
    QStringList load_group_list();      // 信号ツリーのグループ名(tree_groups.txt、無ければ既定)
private slots:
    void on_pushButtonGet_clicked();        // 日時 A と B のデータを取得する
    void on_pushButtonNowA_clicked();
    void on_pushButtonNowB_clicked();
    void on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column);
    void on_pushButtonDelete_clicked();
    void on_listWidget_doubleClicked(const QModelIndex &index);   // ダブルクリックした信号を先頭(基準信号)にする
    void on_spinThreshold_valueChanged(double value);     // 閾値が変わったら、判定だけ描き直す
    void on_checkBoxSameY_toggled(bool checked);
    void on_checkBoxCenter_toggled(bool checked);         // 各期間の平均を引いて、真ん中にそろえて表示する
    void result_row_activated(int row,int column);        // 一覧の行をダブルクリックで、その信号のグラフへスクロール

    void show_tag_offset();     // File メニューの Tag Offset...(sacla_tag, sr_tag のオフセット)
    void check_config_files();  // 起動時に設定ファイルの有無を確認して警告する
    void load_colors();         // 起動時に settings.ini の [Colors] から色を読む(再起動で切り替わる)
    void open_config_files();   // File メニュー: 設定ディレクトリのテキストファイルを pluma で全部開く

private:
    QString signal_list_file(const QString &name);
    bool load_signal_list(QListWidget *list, const QString &name);
    void save_signal_list(QListWidget *list, const QString &name);
    void connect_save_signal_list(QListWidget *list, const QString &name);
    void createActions();
    void createStatusBar();

    // 時間幅の入力(数値 + 単位)を秒にする
    qint64 width_seconds(QSpinBox *spin,QComboBox *unit) const;
    // end を終端とする width_sec 秒の範囲のショットを取得する(リストの全信号)。呼び出し側が delete する
    Shot_data *fetch_shots(const QDateTime &end,qint64 width_sec);

    // 信号ごとの行に、A(左)と B(右)のグラフを並べるスクロール領域
    void create_plot_area();
    void fit_row_height();              // 領域の大きさに合わせて行の高さを決め直す
    void ensure_plot_rows(int n);       // n 行以上にする(足りない分だけ作る。減らしはしない)
    void plot_init(QCustomPlot *plot,const QColor &color);
    QScrollArea *plot_scroll = nullptr;
    QWidget *plot_container = nullptr;
    QGridLayout *plot_grid = nullptr;
    int plot_row_h = 170;               // 1 行の高さ(スクロール領域の見える高さの 1/3)
    // 1 行 = 4 列: A のトレンド | A の散布図 | B のトレンド | B の散布図
    // 散布図は、横軸がその行の信号、縦軸が基準信号(リストの一番上の信号)。同じショットどうしの組
    QVector<QCustomPlot*> plots_a;      // 日時 A のトレンド
    QVector<QCustomPlot*> scatter_a;    // 日時 A の散布図
    QVector<QCustomPlot*> plots_b;      // 日時 B のトレンド
    QVector<QCustomPlot*> scatter_b;    // 日時 B の散布図
    void scatter_init(QCustomPlot *plot,const QColor &color);
    void refresh_reference_font();      // リストの先頭(基準信号)だけを太字にする

    // 取得済みのデータから、グラフ・一覧・通知を描き直す(DB は読まない)
    void update_view();
    void load_difference_settings();    // settings.ini の [Difference] から閾値などを読む
    void create_result_table();
    struct PlotItems{
        QCPItemText *text = nullptr;    // 左上の注記(個数・平均・標準偏差)。1 つ作って使い回す
        QCPItemRect *band = nullptr;    // 平均 ± 標準偏差の帯
    };
    QMap<QCustomPlot*,PlotItems> plot_items;
    void draw_plot(QCustomPlot *plot,const QVector<double> &t,const QVector<double> &v,const VarStats &st,
                   double xbeg,double xend,double ylo,double yhi,const QString &signal,
                   const QColor &color,VarVerdict verdict,double offset);
    // 散布図 1 つを描く。x, y は同じショットの値の組(引く前の値)。xoff, yoff は表示のとき引く量(Center がオフなら 0)、
    // xlo..yhi は引いたあとの値での範囲。reference=true は基準信号の行(自分自身との散布図は無意味なので空にする)
    void draw_scatter(QCustomPlot *plot,const std::vector<double> &x,const std::vector<double> &y,
                      double xlo,double xhi,double ylo,double yhi,double xoff,double yoff,
                      const QString &xname,const QString &yname,VarVerdict verdict,bool reference);
    QTableWidget *result_table = nullptr;
    QStringList view_signals;           // 最後に Get したときの信号(行の順)
    double win_a_beg = 0,win_a_end = 0; // 日時 A・B の期間(エポック秒)
    double win_b_beg = 0,win_b_end = 0;
    VarParams var_params;               // 閾値は画面の値。有意水準と最小個数は settings.ini

    long long tag_off_sacla = 0;        // sacla_tag のオフセット(既定0)。A と B で共通
    long long tag_off_sr = 0;           // sr_tag のオフセット(既定0)
    Ui::MainWindow *ui;
    Shot_data *data_a = nullptr;
    Shot_data *data_b = nullptr;
};
#endif // MAINWINDOW_H
