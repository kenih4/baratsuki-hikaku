#include "shot_data.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

// データ取得時にタグを決めたショットの情報を、ターミナル(標準エラー)に1行で出す。
//   evno : 基準にした sr_mon_screen_sep5u_otr/intensity のイベント番号
//   window: ショットの検索時間窓 beg_time ~ end_time
//   sacla_tag, sr_tag : 決まったタグ(0 は見つからなかったことを示す)
// タグのオフセットを指定しているときは、その旨と、実際に照合に使うタグを明示する。
static void log_tag(long long evno,const Data &d,const char *note,long long off_sacla,long long off_sr)
{
    cerr << "[tag] evno=" << evno
         << "  window=" << (d.beg_time.empty() ? "-" : d.beg_time) << " ~ " << (d.end_time.empty() ? "-" : d.end_time)
         << "  sacla_tag=" << d.sacla_tag
         << "  sr_tag=" << d.sr_tag;
    if(off_sacla!=0 || off_sr!=0){
        cerr << "  [OFFSET SPECIFIED: sacla_tag" << showpos << off_sacla << noshowpos;
        if(d.sacla_tag!=0)cerr << " -> " << d.sacla_tag+off_sacla;
        else cerr << " (not applied: tag not found)";
        cerr << ", sr_tag" << showpos << off_sr << noshowpos;
        if(d.sr_tag!=0)cerr << " -> " << d.sr_tag+off_sr;
        else cerr << " (not applied: tag not found)";
        cerr << "]";
    }
    if(note && note[0])cerr << "  " << note;
    cerr << endl;
}

Shot_data::Shot_data()
{
}

Shot_data::~Shot_data(){

}

void Shot_data::get_tag(string begtime,string endtime)
{
    string sep5u = "sr_mon_screen_sep5u_otr/intensity";
    string route = "xfel_tmg_sw_master_ema_route_inf_shm/current";
    string ssbt = "sr_safety_topup_intlk_beam_ssbt1/charge";
    Db_f_data *get_inf_sep5u;
    Db_i_data *get_inf_route;
    Db_f_data *get_inf_ssbt;
    int get_inf_cnt_sep5u = 0,get_inf_cnt_route = 0,get_inf_cnt_ssbt = 0;
    int status;
    char out[256];
    uint64_t btime;

    data.clear();
    status = db_get_data_f(sep5u.c_str(),begtime.c_str(),endtime.c_str(),&get_inf_sep5u,&get_inf_cnt_sep5u);
    if(status==0 && get_inf_cnt_sep5u>0){
        data.resize(get_inf_cnt_sep5u);
        for(int i=0;i<get_inf_cnt_sep5u;i++){
            DLOG<<(get_inf_sep5u+i)->evno << "  " << (get_inf_sep5u+i)->tdata<<endl;
            db_unixtime_us_to_date((get_inf_sep5u+i)->unixtime_us,out);
            status = db_get_data_num_i(route.c_str(),out,60,&get_inf_route,&get_inf_cnt_route);
            if(status==0 && get_inf_cnt_route>0){
                for(int j=0;j<get_inf_cnt_route;j++){
                    if((get_inf_route+j)->idata==769){
                        data[i].sacla_tag=(get_inf_route+j)->evno;
                        db_unixtime_us_to_date((get_inf_route+get_inf_cnt_route-1)->unixtime_us,out);
                        data[i].beg_time = out;
                        db_unixtime_us_add_sec(get_inf_route->unixtime_us,2.0,&btime);
                        db_unixtime_us_to_date(btime,out);
                        data[i].end_time = out;
                        status = db_get_data_f(ssbt.c_str(),data[i].beg_time.c_str(),data[i].end_time.c_str(),&get_inf_ssbt,&get_inf_cnt_ssbt);
                        if(status == 0 && get_inf_cnt_ssbt>0){
                            for(int n=0;n<get_inf_cnt_ssbt;n++){
                                if((get_inf_ssbt+n)->fdata > 0.0){
                                    data[i].sr_tag = (get_inf_ssbt+n)->evno;
                                    break;
                                }
                            }
                            free(get_inf_ssbt);
                        }
                        break;
                    }
                }
                free(get_inf_route);
            }
            log_tag((get_inf_sep5u+i)->evno,data[i],
                    data[i].sacla_tag==0 ? "(route 769 not found)" : (data[i].sr_tag==0 ? "(sr_tag not found)" : ""),
                    off_sacla,off_sr);
        }
        free(get_inf_sep5u);
    }
    // route 769 が見つからなかったショットは、検索時間窓も無く、値を取れないので除く
    size_t before = data.size();
    data.erase(remove_if(data.begin(),data.end(),[](const Data &d){return d.sacla_tag==0;}),data.end());
    if(data.size()!=before)cerr << "[tag] " << before-data.size() << " shot(s) without sacla_tag were dropped" << endl;
    sort(data.begin(),data.end());
}

void Shot_data::db_get_sync_data()
{
    int status;
    DB_SIGNAL_STRUCT siglist;
    DB_SIGNAL_DATA* tData;
    Db_f_data *fdata;

    status = db_init_signal_list(&siglist);
    if(status != 0)return;
    for(auto itr = sig.begin();itr != sig.end();){
        status = db_add_signal_list(&siglist,itr->name.c_str());
        if(status != 0){
            printf("err db_add_signal_list :: %s\n",itr->name.c_str());
            itr = sig.erase(itr);       // DB に無い信号は外す
        }else{
            ++itr;
        }
    }
    DLOG<<"num_siglist  =  "<<siglist.num_signal<<endl;
    for(int n=0;n<(int)data.size();n++){
        status = db_get_data_multi(&siglist,data[n].beg_time.c_str(),data[n].end_time.c_str(),1,0);
        if(status == 0){
            for(int i=0;i<(int)sig.size();i++){
                for(int j=0;j<siglist.num_signal;j++){
                    if(sig[i].name.compare(siglist.signal_list[j].sig_name)==0){
                        tData = &siglist.signal_list[j];
                        fdata = (Db_f_data*)tData->data;
                        for(int m=0;m<tData->num_data;m++){
                            if(fdata[m].evno == eff_sacla(n)||
                                    fdata[m].evno == eff_sr(n)){
                                if((fdata[m].fdata>1e8||sig[i].name.find("sr_safety")!=std::string::npos)&&sig[i].name.find("efficiency")==std::string::npos){
                                    data[n].value[sig[i].name] = fdata[m].fdata*1.60219e-10;
                                }else{
                                    data[n].value[sig[i].name] = fdata[m].fdata;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    db_release_signal_list_all(&siglist);
}

// 時間順に並んだ v から移動平均(中心、端は窓を狭める)を引いて、ゆっくりしたドリフトを除く
void Shot_data::detrend(double *v,int n)
{
    if(n<6)return;
    int w = n/10;
    if(w<5)w = 5;
    if(w>25)w = 25;
    int h = w/2;
    vector<double> ps(n+1,0.0);
    for(int i=0;i<n;i++)ps[i+1] = ps[i]+v[i];
    vector<double> out(n);
    for(int i=0;i<n;i++){
        int lo = max(0,i-h), hi = min(n-1,i+h);
        out[i] = v[i]-(ps[hi+1]-ps[lo])/(hi-lo+1);
    }
    for(int i=0;i<n;i++)v[i] = out[i];
}

// 最小/最大から比率 ratio だけ広げた範囲を1回の走査で求める
void Shot_data::get_range(int n,double ratio,double &lo,double &hi)
{
    lo = hi = 0;
    if(n<0 || n>=(int)sig.size())return;
    const string &nm = sig[n].name;
    bool ok = false;
    double mx = 0,mn = 0;
    for(int i=0;i<(int)data.size();i++){
        double v = value(nm,i);
        if(!std::isfinite(v))continue;
        if(!ok){mx = mn = v;ok = true;}
        else{
            if(mx<v)mx = v;
            if(mn>v)mn = v;
        }
    }
    if(!ok)return;
    lo = mn - (mx - mn) * ratio;
    hi = mx + (mx - mn) * ratio;
}

void Shot_data::outfile(string fname)
{
    QDir().mkpath(QFileInfo(fname.c_str()).absolutePath());     // 保存先ディレクトリが無ければ作る
    QFile fp(fname.c_str());
    if(fp.open(QIODevice::WriteOnly)){
        QTextStream out(&fp);
        out << "sacla_tag"<<",";
        out << "sr_tag"<<",";
        out << "beg_time"<<",";
        out << "end_time"<<",";
        for(int j=0;j<(int)sig.size();j++){
            out << sig[j].name.c_str()<<",";
        }
        out << Qt::endl;
        for(int i=0;i<(int)data.size();i++){
            out << data[i].sacla_tag<<",";
            out << data[i].sr_tag<<",";
            string bt = data[i].beg_time, et = data[i].end_time;
            replace(bt.begin(),bt.end(),' ','-');
            replace(et.begin(),et.end(),' ','-');
            out << bt.c_str()<<",";
            out << et.c_str()<<",";
            for(int j=0;j<(int)sig.size();j++){
                double v = value(sig[j].name,i);
                if(std::isfinite(v))out << v;      // 欠測は空欄
                out << ",";
            }
            out << Qt::endl;
        }
        fp.close();
    }else{
        qWarning().noquote()<<"Cannot open file  "<<fname.c_str()<<Qt::endl;
    }

}
