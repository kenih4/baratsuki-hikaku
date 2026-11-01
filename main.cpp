#include "mainwindow.h"
#include "period_args.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 設定ファイルの保存先(既定は ~/.config/baratsuki-hikaku)を実行ファイル名に依存させない
    QCoreApplication::setApplicationName("baratsuki-hikaku");

    // ウィンドウのアイコン(abcicon.png。リソースに埋め込み)
    a.setWindowIcon(QIcon(":/icon/abcicon.png"));

    // 設定ファイルのディレクトリは、引数で変えられる:
    //   baratsuki-hikaku /path/to/dir
    //   baratsuki-hikaku -c /path/to/dir      (--config-dir /path/to/dir, --config-dir=/path/to/dir)
    QCommandLineParser parser;
    parser.setApplicationDescription("Compare the variation of signals between two periods (A and B)");
    parser.addHelpOption();
    QCommandLineOption cfg(QStringList() << "c" << "config-dir",
                           "Directory of the config files (default: ~/.config/baratsuki-hikaku).","dir");
    parser.addOption(cfg);
    parser.addPositionalArgument("dir","Config directory (same as --config-dir).","[dir]");

    // 日時 A・B(それぞれ期間の終端)と時間幅。指定しなかったものは画面の既定のまま
    //   baratsuki-hikaku -a "2026/11/01 09:00:00" --a-width 1h -b now --b-width 90m --get
    QCommandLineOption aTime(QStringList() << "a" << "a-time",
                             "End of period A: 'yyyy/MM/dd HH:mm[:ss]', 'yyyy-MM-dd HH:mm[:ss]' or 'now' (default: 1 day ago).","datetime");
    QCommandLineOption aWidth("a-width",
                              "Width of period A: a number with m, h or d (90, 30m, 2h, 7d; no unit = minutes) (default: 60m).","width");
    QCommandLineOption bTime(QStringList() << "b" << "b-time",
                             "End of period B (same format as --a-time) (default: now).","datetime");
    QCommandLineOption bWidth("b-width",
                              "Width of period B (same format as --a-width) (default: 60m).","width");
    QCommandLineOption getNow(QStringList() << "g" << "get",
                              "Fetch the data right after start (same as pressing Get).");
    parser.addOption(aTime);
    parser.addOption(aWidth);
    parser.addOption(bTime);
    parser.addOption(bWidth);
    parser.addOption(getNow);
    parser.process(a);

    QString dir = parser.value(cfg);
    const QStringList pos = parser.positionalArguments();
    if(dir.isEmpty() && !pos.isEmpty())dir = pos.first();
    if(pos.size()>1 || (!parser.value(cfg).isEmpty() && !pos.isEmpty())){
        fprintf(stderr,"Error: specify the config directory only once (either --config-dir or a positional argument).\n");
        return 1;
    }
    if(!dir.isEmpty())MainWindow::set_config_dir(QDir(dir).absolutePath());

    // 期間の引数。読めないときは、画面を出さずにエラーで終わる
    MainWindow::InitialPeriod period[2];        // [0] = A, [1] = B
    const QCommandLineOption *time_opt[2] = {&aTime,&bTime};
    const QCommandLineOption *width_opt[2] = {&aWidth,&bWidth};
    for(int k=0;k<2;k++){
        QString err;
        if(parser.isSet(*time_opt[k]) && !parse_period_time(parser.value(*time_opt[k]),period[k].end,err)){
            fprintf(stderr,"Error: --%s-time: %s\n",k==0?"a":"b",qPrintable(err));
            return 1;
        }
        if(parser.isSet(*width_opt[k]) && !parse_period_width(parser.value(*width_opt[k]),period[k].width,period[k].unit_sec,err)){
            fprintf(stderr,"Error: --%s-width: %s\n",k==0?"a":"b",qPrintable(err));
            return 1;
        }
    }
    MainWindow::set_initial_period(false,period[0]);
    MainWindow::set_initial_period(true,period[1]);
    MainWindow::set_auto_get(parser.isSet(getNow));

    MainWindow w;
    w.show();
    return a.exec();
}
