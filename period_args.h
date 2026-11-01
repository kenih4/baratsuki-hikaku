#ifndef PERIOD_ARGS_H
#define PERIOD_ARGS_H
#include <QDateTime>
#include <QString>

// コマンドライン引数で指定する、日時 A・B と時間幅の文字列の解釈(QtCore だけに依存。単体で試験できる)

// 期間の終端の日時。次の形式を受け付ける(秒は省略可):
//   now
//   yyyy/MM/dd HH:mm[:ss]     yyyy-MM-dd HH:mm[:ss]     yyyy-MM-ddTHH:mm[:ss]
// 読めなければ false を返し、err に理由を入れる
bool parse_period_time(const QString &s,QDateTime &out,QString &err);

// 時間幅。「数値 + 単位」で、単位は m(分)、h(時間)、d(日)。単位を省くと分。
//   例: 90   30m   2h   1.5h   7d
// value/unit_sec は、画面の入力欄(整数 1〜100000 と単位)にそのまま入れられる形で返す。
// 小数は、全体が整数の分になるときだけ受け付け、分に直す(1.5h → 90 分)。
// 読めない・0 以下・1 分の倍数でない・入力欄に入らない大きさなら false
bool parse_period_width(const QString &s,int &value,qint64 &unit_sec,QString &err);

#endif // PERIOD_ARGS_H
