#include "variation.h"
#include <algorithm>

using std::vector;

static vector<double> finite_only(const vector<double> &v)
{
    vector<double> r;
    r.reserve(v.size());
    for(double x : v)if(std::isfinite(x))r.push_back(x);
    return r;
}

// 昇順に並んだ s の q 分位点(0〜1、線形補間)
static double quantile_sorted(const vector<double> &s,double q)
{
    if(s.empty())return NAN;
    double pos = q*(s.size()-1);
    size_t lo = (size_t)std::floor(pos),hi = (size_t)std::ceil(pos);
    return s[lo] + (s[hi]-s[lo])*(pos-lo);
}

static double median_of(vector<double> v)
{
    std::sort(v.begin(),v.end());
    return quantile_sorted(v,0.5);
}

VarStats var_stats(const vector<double> &in)
{
    VarStats r;
    vector<double> v = finite_only(in);
    r.n = (int)v.size();
    if(r.n==0)return r;
    double sum = 0;
    for(double x : v)sum += x;
    r.mean = sum/r.n;
    if(r.n>=2){
        double ss = 0;
        for(double x : v)ss += (x-r.mean)*(x-r.mean);
        r.sd = std::sqrt(ss/(r.n-1));
        if(r.mean!=0)r.cv = r.sd/std::fabs(r.mean);
    }
    vector<double> s = v;
    std::sort(s.begin(),s.end());
    r.median = quantile_sorted(s,0.5);
    vector<double> dev(v.size());
    for(size_t i=0;i<v.size();i++)dev[i] = std::fabs(v[i]-r.median);
    r.mad = median_of(dev);
    r.iqr = quantile_sorted(s,0.75) - quantile_sorted(s,0.25);
    return r;
}

// 正則化不完全ベータ関数 I_x(a,b) の連分数(Numerical Recipes の方法)
static double betacf(double a,double b,double x)
{
    const int MAXIT = 500;
    const double EPS = 3e-16,FPMIN = 1e-300;
    double qab = a+b,qap = a+1,qam = a-1;
    double c = 1,d = 1-qab*x/qap;
    if(std::fabs(d)<FPMIN)d = FPMIN;
    d = 1/d;
    double h = d;
    for(int m=1;m<=MAXIT;m++){
        int m2 = 2*m;
        double aa = m*(b-m)*x/((qam+m2)*(a+m2));
        d = 1+aa*d;if(std::fabs(d)<FPMIN)d = FPMIN;
        c = 1+aa/c;if(std::fabs(c)<FPMIN)c = FPMIN;
        d = 1/d;
        h *= d*c;
        aa = -(a+m)*(qab+m)*x/((a+m2)*(qap+m2));
        d = 1+aa*d;if(std::fabs(d)<FPMIN)d = FPMIN;
        c = 1+aa/c;if(std::fabs(c)<FPMIN)c = FPMIN;
        d = 1/d;
        double del = d*c;
        h *= del;
        if(std::fabs(del-1)<EPS)break;
    }
    return h;
}

static double ibeta(double a,double b,double x)
{
    if(x<=0)return 0;
    if(x>=1)return 1;
    double bt = std::exp(std::lgamma(a+b)-std::lgamma(a)-std::lgamma(b)+a*std::log(x)+b*std::log1p(-x));
    if(x<(a+1)/(a+b+2))return bt*betacf(a,b,x)/a;
    return 1-bt*betacf(b,a,1-x)/b;
}

double brown_forsythe_p(const vector<double> &ain,const vector<double> &bin)
{
    vector<double> a = finite_only(ain),b = finite_only(bin);
    int na = (int)a.size(),nb = (int)b.size(),N = na+nb;
    if(na<2 || nb<2 || N<3)return NAN;
    // 各群の中央値からの絶対偏差 z について、1 元配置の分散分析(2 群)を行う
    double ma = median_of(a),mb = median_of(b);
    vector<double> za(na),zb(nb);
    double sa = 0,sb = 0;
    for(int i=0;i<na;i++){za[i] = std::fabs(a[i]-ma);sa += za[i];}
    for(int i=0;i<nb;i++){zb[i] = std::fabs(b[i]-mb);sb += zb[i];}
    double za_bar = sa/na,zb_bar = sb/nb,z_bar = (sa+sb)/N;
    double between = na*(za_bar-z_bar)*(za_bar-z_bar) + nb*(zb_bar-z_bar)*(zb_bar-z_bar);
    double within = 0;
    for(double z : za)within += (z-za_bar)*(z-za_bar);
    for(double z : zb)within += (z-zb_bar)*(z-zb_bar);
    if(within<=0)return between>0 ? 0.0 : 1.0;      // 群内のばらつきが 0: 群間に差があれば確実に有意
    double d1 = 1,d2 = N-2;
    double F = between/d1/(within/d2);
    return ibeta(d2/2,d1/2,d2/(d2+d1*F));
}

VarCompare var_compare(const vector<double> &a,const vector<double> &b,const VarParams &p)
{
    VarCompare r;
    r.a = var_stats(a);
    r.b = var_stats(b);
    if(r.a.n<p.min_n || r.b.n<p.min_n){
        r.note = r.a.n<p.min_n && r.b.n<p.min_n ? "too few data in A and B"
               : (r.a.n<p.min_n ? "too few data in A" : "too few data in B");
        return r;
    }
    bool ca = !(r.a.sd>0),cb = !(r.b.sd>0);
    if(ca || cb){
        r.note = ca && cb ? "constant in A and B" : (ca ? "constant in A" : "constant in B");
        return r;
    }
    r.ratio = r.b.sd/r.a.sd;
    r.fold = r.ratio>=1 ? r.ratio : 1/r.ratio;
    if(r.a.iqr>0)r.iqr_ratio = r.b.iqr/r.a.iqr;
    if(r.a.cv>0 && std::isfinite(r.b.cv))r.cv_ratio = r.b.cv/r.a.cv;
    r.pval = brown_forsythe_p(a,b);
    if(r.fold>=p.threshold){
        r.verdict = (std::isfinite(r.pval) && r.pval<p.alpha) ? VAR_LARGE : VAR_LARGE_NS;
    }else{
        r.verdict = VAR_SAME;
    }
    return r;
}
