#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <qdatetime.h>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QSettings>
#include <QProcess>
#include <QMenu>
#include <QSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollBar>
#include <QWheelEvent>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <cmath>
#include <algorithm>

// 日時 A と日時 B のグラフの色(settings.ini の [Colors] で変えられる。load_colors。再起動で反映)
static QColor COLOR_A(0,105,107);       // 暗い青緑(白い背景でのコントラスト比 6.5)
static QColor COLOR_B(200,90,0);        // オレンジ

// 設定ファイルのディレクトリ。実行時の引数で指定されたときはそれ、なければ ~/.config/baratsuki-hikaku
static QString g_config_dir;

// 起動時の日時と時間幅(コマンドライン引数。main で設定する)
static MainWindow::InitialPeriod g_initial_a,g_initial_b;
static bool g_auto_get = false;

void MainWindow::set_initial_period(bool is_b,const InitialPeriod &p)
{
    (is_b ? g_initial_b : g_initial_a) = p;
}

void MainWindow::set_auto_get(bool on)
{
    g_auto_get = on;
}

// 日時の文字列の形式(DB API に渡す形式でもある)
static const char *DATE_FORMAT = "yyyy/MM/dd HH:mm:ss";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    cerr << "Config directory : " << QFileInfo(signal_list_file("signals.txt")).absolutePath().toStdString()
         << (g_config_dir.isEmpty() ? "  (default)" : "  (specified by argument)") << endl;
    load_colors();      // plot_init などで色を使う前に、settings.ini の色を読む

    int status = db_init();
    cerr << "#### DB Init : " << status << endl;
    if(DB_OK != status)
    {
        exit(1);
    }

    // 日時 A・B は、それぞれのデータの終端。時間幅の既定は 60 分(過去 1 時間)
    QDateTime now = QDateTime::currentDateTime();
    ui->dateTimeEditB->setDateTime(now);
    ui->dateTimeEditA->setDateTime(now.addDays(-1));
    for(QDateTimeEdit *e : {ui->dateTimeEditA,ui->dateTimeEditB})e->setDisplayFormat(tr(DATE_FORMAT));
    for(QComboBox *c : {ui->comboUnitA,ui->comboUnitB}){
        c->addItem(tr("min"),60);
        c->addItem(tr("hour"),3600);
        c->addItem(tr("day"),86400);
        c->setCurrentIndex(0);
    }
    // コマンドライン引数で指定されたものを入れる(指定なしの項目は、上の既定のまま)
    struct Target{const InitialPeriod *p;QDateTimeEdit *end;QSpinBox *width;QComboBox *unit;};
    for(const Target &t : {Target{&g_initial_a,ui->dateTimeEditA,ui->spinWidthA,ui->comboUnitA},
                           Target{&g_initial_b,ui->dateTimeEditB,ui->spinWidthB,ui->comboUnitB}}){
        if(t.p->end.isValid())t.end->setDateTime(t.p->end);
        int idx = t.unit->findData((qlonglong)t.p->unit_sec);
        if(t.p->width>0 && idx>=0){
            t.width->setValue(t.p->width);
            t.unit->setCurrentIndex(idx);
        }
    }

    create_plot_area();
    create_result_table();
    load_difference_settings();

    load_signal_list(ui->listWidget,"signals.txt");
    // listWidget が変更されたら信号リストファイルに保存する
    connect_save_signal_list(ui->listWidget,"signals.txt");

    create_tree();
    createActions();
    createStatusBar();

    QTimer::singleShot(0, this, SLOT(check_config_files()));
    if(g_auto_get)QTimer::singleShot(0, ui->pushButtonGet, SLOT(click()));      // --get: 起動直後に取得する
}

void MainWindow::set_config_dir(const QString &dir)
{
    g_config_dir = dir;
}

QString MainWindow::signal_list_file(const QString &name)
{
    QString dir = g_config_dir.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            : g_config_dir;
    return dir + "/" + name;
}

// 1行1信号のテキストファイルから list を読み込む。
// ファイルが無い/空の場合は false を返す。行の先頭の語を信号名とし、空白より後ろは使わない
bool MainWindow::load_signal_list(QListWidget *list, const QString &name)
{
    QFile fp(signal_list_file(name));
    if(!fp.open(QIODevice::ReadOnly | QIODevice::Text)){
        return false;
    }
    QTextStream in(&fp);
    while(!in.atEnd()){
        QString line = in.readLine().trimmed();
        if(line.isEmpty() || line.startsWith('#'))continue;
        QStringList tok = line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        list->addItem(tok.first());
    }
    cerr << "Load signal list : " << signal_list_file(name).toStdString()
         << "  (" << list->count() << " signals)" << endl;
    return list->count() > 0;
}

void MainWindow::save_signal_list(QListWidget *list, const QString &name)
{
    QString fname = signal_list_file(name);
    QDir().mkpath(QFileInfo(fname).absolutePath());
    QFile fp(fname);
    if(!fp.open(QIODevice::WriteOnly | QIODevice::Text)){
        qWarning().noquote() << "Cannot write signal list file  " << fname;
        return;
    }
    QTextStream out(&fp);
    for(int i=0;i<list->count();++i){
        out << list->item(i)->text() << Qt::endl;
    }
}

void MainWindow::connect_save_signal_list(QListWidget *list, const QString &name)
{
    auto save = [this, list, name](){ save_signal_list(list, name); };
    connect(list->model(), &QAbstractItemModel::rowsInserted, this, save);
    connect(list->model(), &QAbstractItemModel::rowsRemoved, this, save);
}

MainWindow::~MainWindow()
{
    delete data_a;
    delete data_b;
    delete ui;
}

// settings.ini の値を文字列で読む。QSettings は行の途中のコメントを扱えず、カンマを含む値は
// リストになるので、コメントを取り除き、リストはカンマでつなぎ直して返す。
static QString ini_string(QSettings &st,const QString &key,const QString &def)
{
    if(!st.contains(key))return def;
    QVariant var = st.value(key);
    QString s = (var.metaType().id()==QMetaType::QStringList) ? var.toStringList().join(",") : var.toString();
    int cut = s.indexOf(QRegularExpression("(;|\\s#)"));    // 「;」、または空白の後の「#」から後ろはコメント(「#8000ff」のような色指定は残す)
    if(cut>=0)s = s.left(cut);
    return s.trimmed();
}

// 起動時に settings.ini の [Colors] から、日時 A・B の色を読む(再起動で切り替わる)。
//   [Colors]
//   a=#00696B     ; 日時 A(左)のグラフの色
//   b=#C85A00     ; 日時 B(右)のグラフの色
// 色は「#RRGGBB」または色名(teal, red など)。無い/不正なキーは既定の色のまま。
void MainWindow::load_colors()
{
    QFileInfo fi(signal_list_file("settings.ini"));
    if(!fi.exists())return;
    QSettings st(fi.absoluteFilePath(),QSettings::IniFormat);
    auto get = [&st](const char *key,QColor &col){
        QString s = ini_string(st,key,"");
        if(s.isEmpty())return;
        QColor c(s);
        if(c.isValid())col = c;
        else cerr << "WARNING: settings.ini " << key << "=" << s.toStdString() << " is not a valid color; using the default" << endl;
    };
    get("Colors/a",COLOR_A);
    get("Colors/b",COLOR_B);
}

// 信号ごとの行(左 = 日時 A、右 = 日時 B)を並べるスクロール領域を作る。
// 行の高さは、この領域の見える高さの 1/3(fit_row_height)。3 行を超える分は縦にスクロールして見る。
void MainWindow::create_plot_area()
{
    plot_scroll = new QScrollArea(ui->centralwidget);
    plot_scroll->setWidgetResizable(true);
    plot_scroll->setFrameShape(QFrame::NoFrame);
    plot_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    plot_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    plot_container = new QWidget;
    plot_grid = new QGridLayout(plot_container);
    plot_grid->setContentsMargins(0,0,0,0);
    plot_scroll->setWidget(plot_container);
    plot_scroll->setMinimumHeight(3*plot_row_h+2*plot_grid->spacing()+2);
    plot_scroll->viewport()->installEventFilter(this);     // 大きさが変わったら行の高さを合わせ直す
    ui->verticalLayout_plots->addWidget(plot_scroll);
}

// スクロール領域の見える高さの 1/3 を、1 行分の高さにする。
// 他のウィジェットの高さを測って合わせると、レイアウトが循環して崩れる(TECH_NOTES §4-9)ので、
// 領域自身の高さだけで決める。
void MainWindow::fit_row_height()
{
    if(!plot_scroll)return;
    int vh = plot_scroll->viewport()->height();
    int h = (vh - 2*plot_grid->spacing())/3;
    if(h<100 || h==plot_row_h)return;      // まだ大きさが決まっていない / 変化なし
    plot_row_h = h;
    for(QCustomPlot *p : plots_a)p->setFixedHeight(plot_row_h);
    for(QCustomPlot *p : plots_b)p->setFixedHeight(plot_row_h);
}

void MainWindow::plot_init(QCustomPlot *plot,const QColor &color)
{
    plot->addGraph();
    plot->graph(0)->setPen(QPen(color));
    plot->graph(0)->setLineStyle(QCPGraph::lsNone);
    plot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle,color,Qt::white,5));
    QSharedPointer<QCPAxisTickerDateTime> dateTicker(new QCPAxisTickerDateTime);
    dateTicker->setDateTimeFormat("HH:mm:ss");
    plot->xAxis->setTicker(dateTicker);
    plot->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom);
}

// 行を n 行以上にする(足りない分だけ作る。減らしはしない)
void MainWindow::ensure_plot_rows(int n)
{
    while(plots_a.size()<n){
        int k = plots_a.size();
        QCustomPlot *a = new QCustomPlot(plot_container);
        QCustomPlot *b = new QCustomPlot(plot_container);
        for(QCustomPlot *p : {a,b}){
            p->setMinimumWidth(248);
            p->setFixedHeight(plot_row_h);
            p->installEventFilter(this);        // ホイールでスクロールするため
        }
        plot_init(a,COLOR_A);
        plot_init(b,COLOR_B);
        plot_grid->addWidget(a,k,0);
        plot_grid->addWidget(b,k,1);
        a->show();
        b->show();
        plots_a.append(a);
        plots_b.append(b);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(plot_scroll && obj==plot_scroll->viewport() && event->type()==QEvent::Resize){
        fit_row_height();
    }
    // グラフの上のホイールは、グラフの拡大縮小ではなく領域のスクロールにする(Ctrl+ホイールで拡大縮小)
    if(event->type()==QEvent::Wheel && plot_scroll){
        QCustomPlot *plot = qobject_cast<QCustomPlot*>(obj);
        if(plot && (plots_a.contains(plot) || plots_b.contains(plot))){
            QWheelEvent *we = static_cast<QWheelEvent*>(event);
            if(!(we->modifiers() & Qt::ControlModifier)){
                QScrollBar *bar = plot_scroll->verticalScrollBar();
                bar->setValue(bar->value() - we->angleDelta().y()/2);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj,event);
}

qint64 MainWindow::width_seconds(QSpinBox *spin,QComboBox *unit) const
{
    return (qint64)spin->value() * unit->currentData().toLongLong();
}

Shot_data *MainWindow::fetch_shots(const QDateTime &end,qint64 width_sec)
{
    Shot_data *d = new Shot_data;
    d->set_tag_offset(tag_off_sacla,tag_off_sr);
    for(int i= 0;i<ui->listWidget->count();++i){
        d->add_signal(ui->listWidget->item(i)->text().toStdString());
    }
    d->get_tag(end.addSecs(-width_sec).toString(DATE_FORMAT).toStdString(),
               end.toString(DATE_FORMAT).toStdString());
    d->db_get_sync_data();
    return d;
}

void MainWindow::on_pushButtonGet_clicked()
{
    if(ui->listWidget->count()==0){
        statusBar()->showMessage(tr("Signal list is empty: double-click signals in the tree to add them"));
        return;
    }
    if(tag_off_sacla!=0 || tag_off_sr!=0){
        cerr << "[tag] NOTE: tag offset is specified (sacla_tag" << showpos << tag_off_sacla
             << ", sr_tag" << tag_off_sr << noshowpos << "). Values are matched with tag+offset." << endl;
    }
    const QDateTime end_a = ui->dateTimeEditA->dateTime(),end_b = ui->dateTimeEditB->dateTime();
    const qint64 width_a = width_seconds(ui->spinWidthA,ui->comboUnitA),width_b = width_seconds(ui->spinWidthB,ui->comboUnitB);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    delete data_a;
    delete data_b;
    data_a = fetch_shots(end_a,width_a);
    data_b = fetch_shots(end_b,width_b);
    QApplication::restoreOverrideCursor();

    // 表示に使う信号と期間は、Get した時点のものを覚えておく(あとでリストを変えても、行がずれないように)
    view_signals.clear();
    for(int i=0;i<ui->listWidget->count();++i)view_signals << ui->listWidget->item(i)->text();
    win_a_end = end_a.toMSecsSinceEpoch()/1000.0;
    win_a_beg = win_a_end - width_a;
    win_b_end = end_b.toMSecsSinceEpoch()/1000.0;
    win_b_beg = win_b_end - width_b;

    ensure_plot_rows(view_signals.size());
    fit_row_height();
    update_view();
}

// 時刻の文字列(DB API が返す形式)をエポック秒にする。読めなければ NAN
static double shot_time(const string &s)
{
    QString q = QString::fromStdString(s);
    QDateTime dt = QDateTime::fromString(q,"yyyy/MM/dd HH:mm:ss.z");
    if(!dt.isValid())dt = QDateTime::fromString(q.left(19),"yyyy/MM/dd HH:mm:ss");
    return dt.isValid() ? dt.toMSecsSinceEpoch()/1000.0 : NAN;
}

// 1 信号ぶんの値を集める。値が取れていないショット(NAN)は入れない。
//   vals : 統計用の値(時刻が読めないショットも含む)
//   t, v : グラフ用の(時刻, 値)
static void collect_series(Shot_data *d,const string &name,std::vector<double> &vals,QVector<double> &t,QVector<double> &v)
{
    for(int i=0;i<d->data_size();i++){
        double x = d->value(name,i);
        if(!std::isfinite(x))continue;
        vals.push_back(x);
        double tm = shot_time(d->endtime(i));
        if(std::isfinite(tm)){t.append(tm);v.append(x);}
    }
}

// 値の最小・最大(有限な値だけ)。1 つも無ければ false
static bool value_range(const std::vector<double> &vals,double &lo,double &hi)
{
    bool ok = false;
    for(double x : vals){
        if(!std::isfinite(x))continue;
        if(!ok){lo = hi = x;ok = true;}
        else{lo = std::min(lo,x);hi = std::max(hi,x);}
    }
    return ok;
}

// 1 つのグラフ(信号 1 つ・期間 1 つ)を描く。
//   平均 ± 標準偏差の帯と、左上の注記を重ね、判定に応じて背景に色を付ける
//   offset : 表示するときに、値から引く量(Center がオフなら 0)。ylo, yhi は、引いたあとの値での範囲。
//            注記の数値は、引く前の本当の値のまま
void MainWindow::draw_plot(QCustomPlot *plot,const QVector<double> &t,const QVector<double> &v,const VarStats &st,
                           double xbeg,double xend,double ylo,double yhi,const QString &signal,
                           const QColor &color,VarVerdict verdict,double offset)
{
    QVector<double> shown = v;
    if(offset!=0)for(double &x : shown)x -= offset;
    plot->graph(0)->setData(t,shown);
    plot->xAxis->setRange(xbeg,xend);
    plot->yAxis->setRange(ylo,yhi);
    QFont lf = font();
    lf.setPointSize(7);
    plot->yAxis->setLabelFont(lf);
    plot->yAxis->setLabel(offset!=0 ? tr("%1 (- mean)").arg(signal) : signal);

    QColor bg = Qt::white;
    if(verdict==VAR_LARGE)bg = QColor(255,226,226);         // 差が大きい: 薄い赤
    else if(verdict==VAR_LARGE_NS)bg = QColor(255,243,210); // 大きいが有意でない: 薄い黄
    plot->setBackground(QBrush(bg));

    PlotItems &it = plot_items[plot];
    if(!it.band){
        it.band = new QCPItemRect(plot);
        it.band->setPen(Qt::NoPen);
        it.band->setLayer("grid");          // 点(main)の下に描く
        it.band->topLeft->setTypeX(QCPItemPosition::ptAxisRectRatio);
        it.band->topLeft->setTypeY(QCPItemPosition::ptPlotCoords);
        it.band->bottomRight->setTypeX(QCPItemPosition::ptAxisRectRatio);
        it.band->bottomRight->setTypeY(QCPItemPosition::ptPlotCoords);
    }
    bool has_band = st.n>=2 && std::isfinite(st.sd);
    if(has_band){
        QColor bc = color;
        bc.setAlpha(45);
        it.band->setBrush(QBrush(bc));
        it.band->topLeft->setCoords(0,st.mean-offset+st.sd);
        it.band->bottomRight->setCoords(1,st.mean-offset-st.sd);
    }
    it.band->setVisible(has_band);

    if(!it.text){
        it.text = new QCPItemText(plot);        // 注記は 1 つ作って使い回す(描くたびに作ると重なる)
        it.text->setPositionAlignment(Qt::AlignTop|Qt::AlignLeft);
        it.text->position->setType(QCPItemPosition::ptAxisRectRatio);
        it.text->position->setCoords(0.01,0.01);
        it.text->setPen(Qt::NoPen);
        it.text->setBrush(QBrush(QColor(255,255,255,200)));
        QFont tf = font();
        tf.setPointSize(8);
        it.text->setFont(tf);
    }
    it.text->setText(st.n==0 ? tr("no data")
                             : tr("n=%1  mean=%2  sd=%3").arg(st.n).arg(st.mean,0,'g',5).arg(st.sd,0,'g',4));
    plot->replot(QCustomPlot::rpQueuedReplot);
}

// 取得済みのデータから、グラフ・一覧・通知を描き直す(DB は読まない)。
// Get のあと、閾値や Y 軸の設定を変えたときにも呼ぶ。
void MainWindow::update_view()
{
    if(!data_a || !data_b)return;
    const int n = view_signals.size();
    var_params.threshold = ui->spinThreshold->value();
    const bool same_y = ui->checkBoxSameY->isChecked();
    const bool center = ui->checkBoxCenter->isChecked();

    std::vector<VarCompare> cmp(n);
    std::vector<std::vector<double>> sa(n),sb(n);
    QVector<QVector<double>> ta(n),va(n),tb(n),vb(n);
    for(int i=0;i<n;i++){
        const string name = view_signals[i].toStdString();
        collect_series(data_a,name,sa[i],ta[i],va[i]);
        collect_series(data_b,name,sb[i],tb[i],vb[i]);
        cmp[i] = var_compare(sa[i],sb[i],var_params);
    }

    for(int k=0;k<plots_a.size();k++){
        bool show = k<n;
        plots_a[k]->setVisible(show);
        plots_b[k]->setVisible(show);
        if(!show)continue;
        // 縦軸の範囲: 同じ行の A と B はそろえる(そろえないと、バラつきの大小が見た目で比べられない)
        double lo_a = 0,hi_a = 1,lo_b = 0,hi_b = 1;
        bool ok_a = value_range(sa[k],lo_a,hi_a),ok_b = value_range(sb[k],lo_b,hi_b);
        // Center: 各期間の平均を引いて表示する(平均が違っても、バラつきの幅を重ねて見比べられる)。
        // 引くのは表示だけで、標準偏差・倍率・判定は変わらない
        double off_a = (center && ok_a && std::isfinite(cmp[k].a.mean)) ? cmp[k].a.mean : 0;
        double off_b = (center && ok_b && std::isfinite(cmp[k].b.mean)) ? cmp[k].b.mean : 0;
        if(ok_a){lo_a -= off_a;hi_a -= off_a;}
        if(ok_b){lo_b -= off_b;hi_b -= off_b;}
        if(same_y && ok_a && ok_b){
            lo_a = lo_b = std::min(lo_a,lo_b);
            hi_a = hi_b = std::max(hi_a,hi_b);
        }
        auto pad = [](double &lo,double &hi){
            double d = (hi-lo)*0.1;
            if(!(d>0))d = std::max(std::fabs(lo)*0.05,1.0);     // 値がすべて同じとき
            lo -= d;hi += d;
        };
        if(ok_a)pad(lo_a,hi_a);
        if(ok_b)pad(lo_b,hi_b);
        draw_plot(plots_a[k],ta[k],va[k],cmp[k].a,win_a_beg,win_a_end,lo_a,hi_a,view_signals[k],COLOR_A,cmp[k].verdict,off_a);
        draw_plot(plots_b[k],tb[k],vb[k],cmp[k].b,win_b_beg,win_b_end,lo_b,hi_b,view_signals[k],COLOR_B,cmp[k].verdict,off_b);
    }

    // 一覧: 差が大きい順(判定の重さ → 倍率の大きい順)
    std::vector<int> order(n);
    for(int i=0;i<n;i++)order[i] = i;
    std::stable_sort(order.begin(),order.end(),[&cmp](int x,int y){
        if(cmp[x].verdict!=cmp[y].verdict)return cmp[x].verdict<cmp[y].verdict;
        double fx = std::isfinite(cmp[x].fold) ? cmp[x].fold : -1;
        double fy = std::isfinite(cmp[y].fold) ? cmp[y].fold : -1;
        return fx>fy;
    });
    auto num = [](double v,int prec)->QTableWidgetItem*{
        QTableWidgetItem *it = new QTableWidgetItem;
        if(std::isfinite(v))it->setText(QString::number(v,'g',prec));
        it->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
        return it;
    };
    result_table->setRowCount(n);
    int n_large = 0,n_ns = 0;
    for(int r=0;r<n;r++){
        int i = order[r];
        const VarCompare &c = cmp[i];
        QString verdict;
        QColor bg = Qt::white;
        switch(c.verdict){
        case VAR_LARGE:        verdict = tr("LARGE");n_large++;bg = QColor(255,226,226);break;
        case VAR_LARGE_NS:     verdict = tr("large (not significant)");n_ns++;bg = QColor(255,243,210);break;
        case VAR_SAME:         verdict = tr("-");break;
        case VAR_UNDETERMINED: verdict = tr("n/a: %1").arg(c.note);bg = QColor(235,235,235);break;
        }
        QTableWidgetItem *name = new QTableWidgetItem(view_signals[i]);
        name->setData(Qt::UserRole,i);              // グラフの行
        QTableWidgetItem *items[] = {
            name,
            num(c.a.n,6),num(c.a.sd,4),num(c.b.n,6),num(c.b.sd,4),
            num(c.ratio,3),num(c.fold,3),num(c.iqr_ratio,3),num(c.cv_ratio,3),num(c.pval,3),
            new QTableWidgetItem(verdict)
        };
        for(int col=0;col<11;col++){
            items[col]->setBackground(bg);
            result_table->setItem(r,col,items[col]);
        }
    }
    result_table->resizeColumnsToContents();
    result_table->horizontalHeader()->setStretchLastSection(true);

    // 知らせる: ステータスバーとウィンドウタイトル
    QString msg = tr("A: %1 shots, B: %2 shots  |  %3 signal(s) with large difference (fold >= %4)")
            .arg(data_a->data_size()).arg(data_b->data_size()).arg(n_large).arg(var_params.threshold);
    if(n_ns>0)msg += tr(", %1 more large but not significant").arg(n_ns);
    statusBar()->showMessage(msg);
    setWindowTitle(n_large>0 ? tr("[%1 large] baratsuki-hikaku").arg(n_large) : QString("baratsuki-hikaku"));
}

void MainWindow::on_spinThreshold_valueChanged(double)
{
    update_view();
}

void MainWindow::on_checkBoxSameY_toggled(bool)
{
    update_view();
}

void MainWindow::on_checkBoxCenter_toggled(bool)
{
    update_view();
}

// 差の一覧を作る。列: 信号、A・B それぞれの個数と標準偏差、標準偏差の比と倍率、
// 参考値(四分位範囲の比、変動係数の比)、検定の p 値、判定
void MainWindow::create_result_table()
{
    result_table = new QTableWidget(0,11,ui->centralwidget);
    result_table->setHorizontalHeaderLabels(QStringList()
        << "Signal" << "nA" << "sd A" << "nB" << "sd B"
        << "sd B/A" << "Fold" << "IQR B/A" << "CV B/A" << "p" << "Verdict");
    result_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    result_table->verticalHeader()->setVisible(false);
    result_table->setMinimumHeight(110);
    result_table->setToolTip(tr("Double-click a row to jump to its graphs.\n"
                                "Fold = max(sd B/sd A, sd A/sd B). p: Brown-Forsythe test for equal variance."));
    connect(result_table,&QTableWidget::cellDoubleClicked,this,&MainWindow::result_row_activated);
    ui->verticalLayout_plots->addWidget(result_table);
}

void MainWindow::result_row_activated(int row,int)
{
    QTableWidgetItem *it = result_table->item(row,0);
    if(!it)return;
    int idx = it->data(Qt::UserRole).toInt();
    if(idx>=0 && idx<plots_a.size())plot_scroll->ensureWidgetVisible(plots_a[idx],0,20);
}

// settings.ini の [Difference] から、差が大きいとする倍率などを読む(無い/不正なときは既定)。
//   [Difference]
//   ratio=2.0         ; 標準偏差の比(大きいほう/小さいほう)がこの値以上なら「差が大きい」。画面の値の初期値
//   alpha=0.05        ; 等分散の検定の有意水準。これ未満の p 値なら「有意」
//   min_shots=5       ; A・B のどちらかの個数がこれ未満の信号は、判定しない
void MainWindow::load_difference_settings()
{
    double ratio = 2.0;
    QFileInfo fi(signal_list_file("settings.ini"));
    if(fi.exists()){
        QSettings st(fi.absoluteFilePath(),QSettings::IniFormat);
        bool ok = false;
        double r = ini_string(st,"Difference/ratio","").toDouble(&ok);
        if(ok && r>=1 && r<=1000)ratio = r;
        else if(st.contains("Difference/ratio"))cerr << "WARNING: settings.ini Difference/ratio must be 1 to 1000; using 2" << endl;
        double al = ini_string(st,"Difference/alpha","").toDouble(&ok);
        if(ok && al>0 && al<1)var_params.alpha = al;
        else if(st.contains("Difference/alpha"))cerr << "WARNING: settings.ini Difference/alpha must be between 0 and 1; using 0.05" << endl;
        int mn = ini_string(st,"Difference/min_shots","").toInt(&ok);
        if(ok && mn>=2 && mn<=100000)var_params.min_n = mn;
        else if(st.contains("Difference/min_shots"))cerr << "WARNING: settings.ini Difference/min_shots must be 2 to 100000; using 5" << endl;
    }
    ui->spinThreshold->setValue(ratio);
}

void MainWindow::on_pushButtonNowA_clicked()
{
    ui->dateTimeEditA->setDateTime(QDateTime::currentDateTime());
}

void MainWindow::on_pushButtonNowB_clicked()
{
    ui->dateTimeEditB->setDateTime(QDateTime::currentDateTime());
}

// 起動時に ~/.config/baratsuki-hikaku の設定ファイルを確認し、無いものがあれば警告する。
// 無くても既定の設定で動く。ダイアログは操作を止めない(Get などは裏で続く)。
void MainWindow::check_config_files()
{
    struct Item{const char *file;const char *meaning;};
    const Item items[] = {
        {"signals.txt",             "信号リスト(無ければ空。ツリーから追加)"},
        {"tree_groups.txt",         "信号ツリーのグループ名(既定の14グループを使用)"},
        {"settings.ini",            "日時 A・B のグラフの色、差が大きいとする倍率など(既定の色、2 倍を使用)"},
    };
    QStringList missing;
    for(const Item &it : items){
        if(!QFile::exists(signal_list_file(it.file))){
            missing << QString("  %1 : %2").arg(it.file).arg(QString::fromUtf8(it.meaning));
        }
    }
    if(missing.isEmpty())return;

    QString dir = QFileInfo(signal_list_file("signals.txt")).absolutePath();
    QString text = QString("設定ファイルが見つかりません。既定の設定で動作します。\n\n保存先: %1\n\n%2\n\n"
                           "信号リストは、リストを変更すると自動で保存されます。")
            .arg(dir).arg(missing.join("\n"));
    cerr << "WARNING: config file(s) not found in " << dir.toStdString() << endl;
    statusBar()->showMessage(tr("Config file(s) not found: using defaults"));
    QMessageBox *box = new QMessageBox(QMessageBox::Warning,"baratsuki-hikaku",text,QMessageBox::Ok,this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    // メインウィンドウの真ん中に重ねて出す(ウィンドウマネージャ任せだと離れた所に出て気づきにくい)
    auto center = [this,box](){
        QPoint c = mapToGlobal(rect().center());
        box->move(c.x()-box->frameGeometry().width()/2,c.y()-box->frameGeometry().height()/2);
    };
    box->adjustSize();
    center();
    box->open();        // 閉じるまでこのウィンドウは操作できないが、処理(Getなど)は止めない
    box->raise();
    box->activateWindow();
    QTimer::singleShot(0,box,center);       // 表示後に枠を含めた大きさで置き直す
}

// 設定ディレクトリ(~/.config/baratsuki-hikaku)の中のテキストファイル(*.txt, *.ini)を、
// pluma でまとめて開く(pluma が起動済みなら、タブとして追加される)。
void MainWindow::open_config_files()
{
    QString dir = QFileInfo(signal_list_file("signals.txt")).absolutePath();
    QStringList names = QDir(dir).entryList(QStringList() << "*.txt" << "*.ini",QDir::Files,QDir::Name);
    if(names.isEmpty()){
        QMessageBox::information(this,"baratsuki-hikaku",
            QString("開くテキストファイルがありません。\n\n%1").arg(dir));
        return;
    }
    QStringList files;
    for(const QString &n : names)files << QDir(dir).absoluteFilePath(n);
    if(!QProcess::startDetached("pluma",files)){
        QMessageBox::warning(this,"baratsuki-hikaku",
            QString("pluma を起動できませんでした。pluma がインストールされ、PATH にあるか確認してください。\n\n%1")
            .arg(files.join("\n")));
    }
}

// File メニュー: sacla_tag と sr_tag のオフセットを指定する子パネル(既定は両方0。A と B で共通)。
// 各信号のデータ点を選ぶとき、タグにオフセットを足したイベント番号と照合する(タグの決定自体は変わらない)。
// Apply で設定して、全ショットのデータを取り直す(Get と同じ)。
void MainWindow::show_tag_offset()
{
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Tag Offset");
    QFormLayout *form = new QFormLayout;
    QSpinBox *sacla = new QSpinBox(dlg);
    QSpinBox *sr = new QSpinBox(dlg);
    for(QSpinBox *s : {sacla,sr}){
        s->setRange(-1000000,1000000);
        s->setPrefix(" ");
    }
    sacla->setValue((int)tag_off_sacla);
    sr->setValue((int)tag_off_sr);
    form->addRow("sacla_tag offset", sacla);
    form->addRow("sr_tag offset", sr);

    QLabel *note = new QLabel("信号のデータ点を、タグ + オフセット のイベント番号と照合して選びます。\n"
                              "Apply で設定して、全ショットのデータを取り直します(Get と同じ)。\n"
                              "0 のままなら、これまでと同じです。ターミナルのタグの表示に、\n"
                              "オフセットを指定していることが明示されます。",dlg);
    note->setWordWrap(true);

    QDialogButtonBox *buttons = new QDialogButtonBox(dlg);
    QPushButton *apply = buttons->addButton("Apply",QDialogButtonBox::ApplyRole);
    QPushButton *reset = buttons->addButton("Reset to 0",QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);

    QVBoxLayout *lay = new QVBoxLayout(dlg);
    lay->addLayout(form);
    lay->addWidget(note);
    lay->addWidget(buttons);

    connect(reset,&QPushButton::clicked,dlg,[sacla,sr](){sacla->setValue(0);sr->setValue(0);});
    connect(buttons,&QDialogButtonBox::rejected,dlg,&QDialog::close);
    connect(apply,&QPushButton::clicked,this,[this,sacla,sr](){
        tag_off_sacla = sacla->value();
        tag_off_sr = sr->value();
        cerr << "[tag] tag offset set: sacla_tag" << showpos << tag_off_sacla << ", sr_tag" << tag_off_sr << noshowpos
             << (tag_off_sacla==0 && tag_off_sr==0 ? "  (no offset)" : "  (OFFSET SPECIFIED)") << endl;
        ui->pushButtonGet->click();        // 新しいオフセットで全ショットのデータを取り直す
    });
    dlg->show();
}

// 信号ツリーのダブルクリックで、信号をリストに加える(すでにあれば何もしない)
void MainWindow::on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
    DLOG<<column<<item->text(column).toStdString()<<endl;
    if(item->childCount()!=0)return;
    if(!ui->listWidget->findItems(item->text(column),Qt::MatchExactly).isEmpty())return;
    ui->listWidget->addItem(item->text(column));
}

// 信号ツリーのグループ名。
// 設定ファイル ~/.config/baratsuki-hikaku/tree_groups.txt に、1行1グループ名で書く
// (空行と '#' で始まる行は無視。重複は最初の1つだけ使う)。
// ファイルが無い、または有効な行が1つもないときは、下の既定のグループを使う。起動時に1回だけ読む。
QStringList MainWindow::load_group_list()
{
    QStringList list;
    QFile fp(signal_list_file("tree_groups.txt"));
    if(fp.open(QIODevice::ReadOnly | QIODevice::Text)){
        QTextStream in(&fp);
        while(!in.atEnd()){
            QString line = in.readLine().trimmed();
            if(line.isEmpty() || line.startsWith('#') || list.contains(line))continue;
            list << line;
        }
    }
    if(!list.isEmpty()){
        cerr << "Load tree groups : " << fp.fileName().toStdString()
             << "  (" << list.size() << " groups)" << endl;
        return list;
    }
    return QStringList{"sr_safety_topup","sr_mon_xsbt","sr_mon_img","xfel_mon_bpm","xfel_mon_xsbt","xfel_mon_msbpm","xfel_mon_csr","xfel_mon_blm","xfel_mon_ct","xfel_mon_timing","xfel_llrf_238","xfel_llrf_476","xfel_llrf_lb","xfel_llrf_sb_1"};
}

void MainWindow::create_tree()
{
    ui->treeWidget->setColumnCount(1);
    ui->treeWidget->setHeaderLabel(tr("Signal"));
    QList<QTreeWidgetItem*> items;
    QStringList list = load_group_list();
    Db_sig_inf *get_inf;
    int get_inf_cnt;

    for(int i=0; i<list.size(); ++i)
        items.append(new QTreeWidgetItem(static_cast<QTreeWidget *>(nullptr), QStringList(list.at(i))));

    for(int i=0; i<list.size(); ++i){
        int status = db_get_sig_list_by_sub_grp(const_cast<char*>(list.at(i).toStdString().c_str()),&get_inf,&get_inf_cnt);
        if(status == DB_OK && get_inf_cnt>0){
            for(int j=0;j<get_inf_cnt;++j){
                db_get_data_kind((get_inf+j)->sig_name,&(get_inf+j)->daq_type);
                if((get_inf+j)->daq_type == DAQ_TYPE_SYNC && (get_inf+j)->off_flg == 0){
                    items.append(new QTreeWidgetItem(items[i], QStringList(QString((get_inf+j)->sig_name))));
                }
            }
            free(get_inf);
        }
    }
    ui->treeWidget->insertTopLevelItems(0, items);

}

void MainWindow::on_pushButtonDelete_clicked()
{
    QListWidgetItem* currentItem = ui->listWidget->takeItem( ui->listWidget->currentRow());
    if ( NULL != currentItem )
    {
        delete ( currentItem );
    }
}

void MainWindow::createActions()
{

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *tagOffsetAct = new QAction(tr("Tag &Offset..."), this);
    tagOffsetAct->setStatusTip(tr("Specify offsets added to sacla_tag and sr_tag when matching signal data (default 0)"));
    connect(tagOffsetAct, &QAction::triggered, this, &MainWindow::show_tag_offset);
    fileMenu->addAction(tagOffsetAct);

    QAction *openCfgAct = new QAction(tr("Open &Config Files (pluma)"), this);
    openCfgAct->setStatusTip(tr("Open all text files in the config directory with pluma"));
    connect(openCfgAct, &QAction::triggered, this, &MainWindow::open_config_files);
    fileMenu->addAction(openCfgAct);

    fileMenu->addSeparator();

    const QIcon exitIcon = QIcon::fromTheme("application-exit");
    QAction *exitAct = fileMenu->addAction(exitIcon, tr("E&xit"), this, &QWidget::close);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));

}
void MainWindow::createStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}
