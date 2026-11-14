// DB API の代用関数(試験用)。shot_data.cpp が呼ぶ関数だけを、決まったデータを返すものとして定義する。
// DB のヘッダは読まない(関数は C リンケージで、名前だけで結びつく)。構造体は、使う DB のヘッダの定義と同じ並びの写しにする必要がある
// (並びが違うと、値がずれて読まれる。最初に、shot_data.h を読んだ側の offsetof と見比べるとよい)。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Thrift 版(mdaq_db_thrift/mdaq_db_api_typedef.h)の定義。別の版(stax)には evno_delta があり、並びが違うので注意
typedef struct {int32_t idata;int64_t evno;int32_t err;int32_t tdata;int32_t tmdata;int64_t unixtime_us;} Db_i_data;
typedef struct {float fdata;int64_t evno;int32_t err;int32_t tdata;int32_t tmdata;int64_t unixtime_us;} Db_f_data;
typedef struct {char sig_name[256];int daq_type;int data_kind;int accel_id;int ret_code;int num_data;void *data;} DB_SIGNAL_DATA;
typedef struct {int initialized;int num_signal;DB_SIGNAL_DATA *signal_list;} DB_SIGNAL_STRUCT;

// 試験側から設定・読み取りする
int g_stub_nshots = 7;
int g_calls_sep5u = 0,g_calls_route = 0,g_calls_ssbt = 0,g_calls_multi = 0;

static const int64_t T0 = 43200;        // 12:00:00(1 日の中の秒)
// 時刻の文字列 "2026/11/01 HH:MM:SS.mmm" と、1 日の中の秒の変換
static int key_of(const char *s){
    int h = 0,m = 0,sec = 0;
    sscanf(s+11,"%d:%d:%d",&h,&m,&sec);
    return h*3600+m*60+sec;
}

extern "C" {

int db_unixtime_us_to_date(const uint64_t us,char *out){
    int64_t sec = (int64_t)(us/1000000ULL),ms = (int64_t)(us/1000ULL%1000ULL);
    int d = (int)(sec%86400);
    sprintf(out,"2026/11/01 %02d:%02d:%02d.%03d",d/3600,d/60%60,d%60,(int)ms);
    return 0;
}

int db_unixtime_us_add_sec(const uint64_t src,double sec,uint64_t *out){
    *out = src + (uint64_t)(sec*1e6);
    return 0;
}

// 基準信号(sep5u)は、1 秒おきに g_stub_nshots 点。ssbt は 3 点で、3 点目だけ fdata>0
int db_get_data_f(const char *sig,const char *,const char *,Db_f_data **out,int *cnt){
    if(strstr(sig,"sep5u")){
        g_calls_sep5u++;
        *cnt = g_stub_nshots;
        *out = (Db_f_data*)calloc(*cnt,sizeof(Db_f_data));
        for(int i=0;i<*cnt;i++){
            (*out)[i].evno = 1000+i;
            (*out)[i].unixtime_us = (T0+i)*1000000LL;
        }
        return 0;
    }
    if(strstr(sig,"ssbt")){
        g_calls_ssbt++;
        *cnt = 3;
        *out = (Db_f_data*)calloc(*cnt,sizeof(Db_f_data));
        for(int k=0;k<3;k++){
            (*out)[k].evno = 5000+k;
            (*out)[k].fdata = k==2 ? 5.0f : 0.0f;
        }
        return 0;
    }
    return -1;
}

// route 信号: 4 点。先頭だけ idata==769。evno はショットの時刻から決まる(2000 + 時刻の秒)
int db_get_data_num_i(const char *,const char *end_dt,int,Db_i_data **out,int *cnt){
    g_calls_route++;
    int key = key_of(end_dt);
    *cnt = 4;
    *out = (Db_i_data*)calloc(*cnt,sizeof(Db_i_data));
    for(int j=0;j<4;j++){
        (*out)[j].idata = j==0 ? 769 : 0;
        (*out)[j].evno = 2000+key;
        (*out)[j].unixtime_us = (int64_t)key*1000000LL + j*100000LL;
    }
    return 0;
}

int db_init_signal_list(DB_SIGNAL_STRUCT *l){
    l->initialized = 1;
    l->num_signal = 0;
    l->signal_list = NULL;
    return 0;
}

// 名前が bad で始まる信号は、DB に無いものとして失敗する
int db_add_signal_list(DB_SIGNAL_STRUCT *l,const char *name){
    if(strncmp(name,"bad",3)==0)return -1;
    l->signal_list = (DB_SIGNAL_DATA*)realloc(l->signal_list,(l->num_signal+1)*sizeof(DB_SIGNAL_DATA));
    DB_SIGNAL_DATA &d = l->signal_list[l->num_signal++];
    memset(&d,0,sizeof(d));
    strncpy(d.sig_name,name,255);
    return 0;
}

// 各信号に 2 点: ノイズの点と、ショットのタグに一致する点(値 = 時刻の秒の差 + 100 × 信号の番号)
int db_get_data_multi(DB_SIGNAL_STRUCT *l,const char *beg,const char *,int,int){
    g_calls_multi++;
    int key = key_of(beg);
    for(int j=0;j<l->num_signal;j++){
        DB_SIGNAL_DATA &d = l->signal_list[j];
        free(d.data);
        d.num_data = 2;
        Db_f_data *a = (Db_f_data*)calloc(2,sizeof(Db_f_data));
        a[0].evno = 99999;a[0].fdata = -1;
        a[1].evno = 2000+key;a[1].fdata = (float)(key-T0)+100.0f*j;
        d.data = a;
    }
    return 0;
}

int db_release_signal_list_all(DB_SIGNAL_STRUCT *l){
    for(int j=0;j<l->num_signal;j++)free(l->signal_list[j].data);
    free(l->signal_list);
    l->signal_list = NULL;
    l->num_signal = 0;
    return 0;
}

}
