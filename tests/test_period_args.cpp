// period_args.cpp の単体試験(QtCore だけ。GUI も DB も不要)。
// 実行: g++ -std=c++17 -Wall -Wextra -O1 -g -fPIC -I. -I$QT/include -I$QT/include/QtCore tests/test_period_args.cpp period_args.cpp -L$QT/lib -lQt6Core -Wl,-rpath,$QT/lib -o /tmp/tp && /tmp/tp
//   (QT=/usr/local/qt6/6.4.3/gcc_64)
#include "period_args.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

static bool W(const char *s,int &v,qint64 &u){ QString e; return parse_period_width(s,v,u,e); }
static bool T(const char *s,QDateTime &d){ QString e; return parse_period_time(s,d,e); }

int main(){
    int v; qint64 u; QDateTime d;
    // 時間幅
    CHECK(W("90",v,u) && v==90 && u==60);          // 単位なしは分
    CHECK(W("30m",v,u) && v==30 && u==60);
    CHECK(W("2h",v,u) && v==2 && u==3600);
    CHECK(W("2H",v,u) && v==2 && u==3600);
    CHECK(W("7d",v,u) && v==7 && u==86400);
    CHECK(W(" 15 m ",v,u) && v==15 && u==60);
    CHECK(W("1.5h",v,u) && v==90 && u==60);        // 小数は分に直す
    CHECK(W("0.5d",v,u) && v==720 && u==60);
    CHECK(W("100000",v,u) && v==100000);           // 入力欄の上限
    CHECK(!W("100001",v,u));                       // 入力欄に入らない
    CHECK(!W("0",v,u));
    CHECK(!W("-5",v,u));
    CHECK(!W("abc",v,u));
    CHECK(!W("",v,u));
    CHECK(!W("1.0001m",v,u));                      // 整数の分にならない
    CHECK(!W("10x",v,u));
    CHECK(!W("0.5m",v,u));
    // 日時
    CHECK(T("2026/11/01 12:34:56",d) && d.date()==QDate(2026,11,1) && d.time()==QTime(12,34,56));
    CHECK(T("2026/11/01 12:34",d) && d.time()==QTime(12,34,0));
    CHECK(T("2026-11-01 12:34:56",d) && d.date()==QDate(2026,11,1));
    CHECK(T("2026-11-01T01:02:03",d) && d.time()==QTime(1,2,3));
    CHECK(T("now",d) && qAbs(d.secsTo(QDateTime::currentDateTime()))<5);
    CHECK(T("NOW",d));
    CHECK(!T("2026/13/01 00:00:00",d));            // 13 月
    CHECK(!T("2026/11/01",d));                     // 時刻なし
    CHECK(!T("yesterday",d));
    CHECK(!T("",d));
    printf(fails ? "FAILED %d\n" : "ALL OK\n",fails);
    return fails ? 1 : 0;
}
