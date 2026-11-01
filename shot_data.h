#ifndef SHOT_DATA_H
#define SHOT_DATA_H
#include <vector>
#include <algorithm>
#include <iostream>
#include <string.h>
#include <map>
#include "db_common.h"
#include <cstdlib>
#include <cmath>

using namespace std;

// 環境変数 BARATSUKI_DEBUG が設定されているときだけ詳細ログを出す(出さないときは引数を評価しない)
inline bool debug_on(){
    static const bool d = getenv("BARATSUKI_DEBUG")!=NULL;
    return d;
}
#define DLOG if(!debug_on()){}else cerr

// 1ショット分のデータ。タグ(sacla_tag, sr_tag)のイベント番号で、各信号の値をそろえる
typedef struct Data{
    int64_t sacla_tag = 0;
    int64_t sr_tag = 0;
    string beg_time;
    string end_time;
    map <string,double>value;
    bool operator<(const Data &right)const {
        return sacla_tag == right.sacla_tag ? sr_tag < right.sr_tag : sacla_tag < right.sacla_tag;
    }
}Data;

typedef struct Signal_param{
    string name;
    bool operator==(const Signal_param &other) const {
        return name == other.name;
    }
}Signal_param;

// 時間範囲の中のショットを集めて、信号ごとの値をタグでそろえて持つ。
// 日時 A と日時 B で別々のインスタンスを作る
class Shot_data
{
public:
    Shot_data();
    ~Shot_data();
    // begtime ~ endtime("yyyy/MM/dd HH:mm:ss")の範囲のショットを探して、タグを決める。
    // タグが見つからなかったショット(route 信号に idata==769 が無い)は除く
    void get_tag(string begtime,string endtime);
    void add_signal(string signal){
        Signal_param s = {signal};
        if(find(sig.begin(),sig.end(),s)==sig.end()){
            sig.push_back(s);
        }
    };
    // 各ショットの値を、タグ(sacla_tag / sr_tag)のイベント番号で照合して DB から取得する。
    // DB に無い信号は sig から外れる
    void db_get_sync_data();
    int data_size(){return data.size();};
    int sig_size(){return sig.size();};
    // 範囲外の番号でも落ちないようにする(空の名前を返す)
    const string &sig_name(int n){return (n>=0 && n<(int)sig.size()) ? sig[n].name : empty_name;};
    // 取得できていない値(信号が無い・ショット番号が範囲外)は NAN を返す(0 にはしない)
    double value(const string &signame,int n){
        if(n<0 || n>=(int)data.size())return NAN;
        auto it = data[n].value.find(signame);     // operator[] は読むだけでも 0 のキーを作るので使わない
        return it==data[n].value.end() ? NAN : it->second;
    };
    string begtime(int n){return (n>=0 && n<(int)data.size()) ? data[n].beg_time : string();};
    string endtime(int n){return (n>=0 && n<(int)data.size()) ? data[n].end_time : string();};
    int64_t sacla_tag(int n){return (n>=0 && n<(int)data.size()) ? data[n].sacla_tag : 0;};
    int64_t sr_tag(int n){return (n>=0 && n<(int)data.size()) ? data[n].sr_tag : 0;};
    // 信号 n の最小・最大から、比率 ratio だけ広げた範囲(NAN は除く)。値が1つも無ければ 0, 0
    void get_range(int n,double ratio,double &lo,double &hi);
    // タグのオフセット: 各信号のデータ点を選ぶとき、タグ(sacla_tag, sr_tag)にこの値を足したイベント番号と照合する。
    // タグが見つかっていない(0)ショットには足さない。タグ自体の決定(get_tag)は変えない
    void set_tag_offset(long long sacla,long long sr){off_sacla = sacla;off_sr = sr;};
    long long tag_offset_sacla(){return off_sacla;};
    long long tag_offset_sr(){return off_sr;};
    // 時間順に並んだ v から移動平均を引いて、ゆっくりしたドリフトを除く
    static void detrend(double *v,int n);
    void outfile(string);
private:
    vector <Signal_param> sig;
    vector<Data> data;
    string empty_name;
    long long off_sacla = 0;    // sacla_tag のオフセット
    long long off_sr = 0;       // sr_tag のオフセット
    long long eff_sacla(int n){return data[n].sacla_tag!=0 ? data[n].sacla_tag+off_sacla : 0;};   // オフセット込みで照合するタグ
    long long eff_sr(int n){return data[n].sr_tag!=0 ? data[n].sr_tag+off_sr : 0;};
};

#endif // SHOT_DATA_H
