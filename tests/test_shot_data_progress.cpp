// Shot_data の進捗通知・キャンセルの試験。DB は tests/stub_db.cpp の代用関数。
// 実行(DB のヘッダの場所は環境に合わせる。PRJ=/2010/out/<版>/prj/ubuntu22pd64、QT=/usr/local/qt6/6.4.3/gcc_64):
//   INC=$(for d in mdaq_daq_thrift mdaq_daq mdaq_db_thrift mdaq_db alarm_mdaq mdaq_paramset emlibs codama/ms_mqtt; do printf -- "-I$PRJ/include/$d "; done)
//   g++ -std=gnu++17 -Wall -Wextra -O1 -g -fPIC -fsanitize=address,undefined $INC -I. -I$QT/include -I$QT/include/QtCore
//   (続き) tests/test_shot_data_progress.cpp tests/stub_db.cpp shot_data.cpp -L$QT/lib -lQt6Core -Wl,-rpath,$QT/lib -o /tmp/tsd && /tmp/tsd
#include "shot_data.h"
#include <cstdio>
#include <string>
#include <vector>

extern int g_stub_nshots,g_calls_sep5u,g_calls_route,g_calls_ssbt,g_calls_multi;

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

static void reset_counters(){g_calls_sep5u = g_calls_route = g_calls_ssbt = g_calls_multi = 0;}

static Shot_data *make(){
    Shot_data *d = new Shot_data;
    d->add_signal("sigA");
    d->add_signal("bad_signal");        // DB に無い信号(db_get_sync_data で外れる)
    d->add_signal("sigC");
    return d;
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    const int N = g_stub_nshots;

    // 1. 進捗なしでも、従来どおり動く(値がタグでそろう。DB に無い信号は外れる)
    {
        reset_counters();
        Shot_data *d = make();
        d->get_tag("b","e");
        d->db_get_sync_data();
        CHECK(!d->canceled());
        CHECK(d->data_size()==N);
        CHECK(d->sig_size()==2);                       // bad_signal が外れた
        for(int i=0;i<N;i++){
            CHECK(d->sacla_tag(i)==2000+43200+i);
            CHECK(d->sr_tag(i)==5002);
            CHECK(d->value("sigA",i)==(double)i);
            CHECK(d->value("sigC",i)==(double)i+100.0);  // 2 番目の信号(番号 1)
        }
        CHECK(!std::isfinite(d->value("bad_signal",0)));
        printf("no progress : shots=%d signals=%d  route calls=%d multi calls=%d\n",d->data_size(),d->sig_size(),g_calls_route,g_calls_multi);
        delete d;
    }

    // 2. 進捗の呼ばれ方: (0,0,0) → (0,0,N) → (0,1,N)…(0,N,N) → (1,0,N) → (1,1,N)…(1,N,N)
    {
        Shot_data *d = make();
        std::vector<std::vector<int>> log;
        d->set_progress([&log](int st,int done,int total){log.push_back({st,done,total});return true;});
        d->get_tag("b","e");
        d->db_get_sync_data();
        std::vector<std::vector<int>> want = {{0,0,0},{0,0,N}};
        for(int i=1;i<=N;i++)want.push_back({0,i,N});
        want.push_back({1,0,N});
        for(int i=1;i<=N;i++)want.push_back({1,i,N});
        CHECK(log==want);
        CHECK(!d->canceled());
        CHECK(d->data_size()==N);
        printf("progress    : %zu calls (expected %zu), first=(%d,%d,%d) last=(%d,%d,%d)\n",log.size(),want.size(),
               log.front()[0],log.front()[1],log.front()[2],log.back()[0],log.back()[1],log.back()[2]);
        delete d;
    }

    // 3. 最初の通知(検索前)でキャンセル: DB を 1 回も呼ばない
    {
        reset_counters();
        Shot_data *d = make();
        d->set_progress([](int,int,int){return false;});
        d->get_tag("b","e");
        CHECK(d->canceled());
        CHECK(g_calls_sep5u==0 && g_calls_route==0);
        CHECK(d->data_size()==0);
        delete d;
    }

    // 4. 総数が分かった直後(0,0,N)でキャンセル: メモリを解放して戻る(ASan/LSan で漏れを検出)
    {
        reset_counters();
        Shot_data *d = make();
        d->set_progress([](int,int done,int total){return !(done==0 && total>0);});
        d->get_tag("b","e");
        CHECK(d->canceled());
        CHECK(g_calls_sep5u==1 && g_calls_route==0);
        CHECK(d->data_size()==0);
        delete d;
    }

    // 5. タグ決定の途中(3 ショット目)でキャンセル: 以降のショットの DB 問い合わせをしない
    {
        reset_counters();
        Shot_data *d = make();
        d->set_progress([](int st,int done,int){return !(st==0 && done==3);});
        d->get_tag("b","e");
        CHECK(d->canceled());
        CHECK(g_calls_route==3 && g_calls_ssbt==3);
        printf("cancel tag  : route calls=%d (stopped at 3 of %d), shots kept=%d\n",g_calls_route,N,d->data_size());
        delete d;
    }

    // 6. 値の取得の途中(2 ショット目)でキャンセル
    {
        Shot_data *d = make();
        d->get_tag("b","e");
        reset_counters();
        d->set_progress([](int st,int done,int){return !(st==1 && done==2);});
        d->db_get_sync_data();
        CHECK(d->canceled());
        CHECK(g_calls_multi==2);
        printf("cancel value: multi calls=%d (stopped at 2 of %d)\n",g_calls_multi,N);
        delete d;
    }

    // 7. 値の取得の開始直前(1,0,N)でキャンセル: db_get_data_multi を呼ばない
    {
        Shot_data *d = make();
        d->get_tag("b","e");
        reset_counters();
        d->set_progress([](int st,int done,int){return !(st==1 && done==0);});
        d->db_get_sync_data();
        CHECK(d->canceled());
        CHECK(g_calls_multi==0);
        delete d;
    }

    printf(fails ? "FAILED %d\n" : "ALL OK\n",fails);
    return fails ? 1 : 0;
}
