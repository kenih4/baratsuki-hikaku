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

    create_plot_area();

    load_signal_list(ui->listWidget,"signals.txt");
    // listWidget が変更されたら信号リストファイルに保存する
    connect_save_signal_list(ui->listWidget,"signals.txt");

    create_tree();
    createActions();
    createStatusBar();

    QTimer::singleShot(0, this, SLOT(check_config_files()));
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    delete data_a;
    delete data_b;
    data_a = fetch_shots(ui->dateTimeEditA->dateTime(),width_seconds(ui->spinWidthA,ui->comboUnitA));
    data_b = fetch_shots(ui->dateTimeEditB->dateTime(),width_seconds(ui->spinWidthB,ui->comboUnitB));
    QApplication::restoreOverrideCursor();

    ensure_plot_rows(ui->listWidget->count());
    fit_row_height();
    statusBar()->showMessage(tr("A: %1 shots, B: %2 shots").arg(data_a->data_size()).arg(data_b->data_size()));
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
        {"settings.ini",            "日時 A・B のグラフの色(既定の色を使用)"},
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
