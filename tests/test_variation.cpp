// variation.cpp の単体試験(Qt も DB も不要)。答えの分かる疑似データで確かめる。
// 実行: g++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS -I. tests/test_variation.cpp variation.cpp -o /tmp/tv && /tmp/tv
#include "variation.h"
#include <random>
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static std::vector<double> gen(std::mt19937 &g,int n,double mu,double sd){
    std::normal_distribution<double> d(mu,sd); std::vector<double> v(n); for(auto &x:v)x=d(g); return v;
}
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    std::mt19937 g(1);
    VarParams p;
    // 1. 同じ分布 → 倍率 ≈ 1、SAME
    { auto a=gen(g,500,10,1), b=gen(g,500,10,1); auto r=var_compare(a,b,p);
      printf("same     : fold=%.3f p=%.3f verdict=%d\n",r.fold,r.pval,r.verdict);
      CHECK(r.fold<1.2); CHECK(r.verdict==VAR_SAME); }
    // 2. σ を 3 倍 → 倍率 ≈ 3、LARGE
    { auto a=gen(g,500,10,1), b=gen(g,500,10,3); auto r=var_compare(a,b,p);
      printf("sd x3    : fold=%.3f ratio=%.3f p=%.3g verdict=%d iqr_ratio=%.3f\n",r.fold,r.ratio,r.pval,r.verdict,r.iqr_ratio);
      CHECK(r.fold>2.7 && r.fold<3.3); CHECK(r.verdict==VAR_LARGE); CHECK(r.pval<1e-6); }
    // 3. 逆向き(B が小さい)でも同じ倍率
    { auto a=gen(g,500,10,3), b=gen(g,500,10,1); auto r=var_compare(a,b,p);
      printf("sd /3    : fold=%.3f ratio=%.3f verdict=%d\n",r.fold,r.ratio,r.verdict);
      CHECK(r.fold>2.7 && r.fold<3.3); CHECK(r.ratio<1); CHECK(r.verdict==VAR_LARGE); }
    // 4. 平均だけずらす → 倍率 ≈ 1(差としない)
    { auto a=gen(g,500,10,1), b=gen(g,500,1000,1); auto r=var_compare(a,b,p);
      printf("mean only: fold=%.3f p=%.3f verdict=%d cv_ratio=%.4f\n",r.fold,r.pval,r.verdict,r.cv_ratio);
      CHECK(r.fold<1.2); CHECK(r.verdict==VAR_SAME); }
    // 5. データ不足
    { auto a=gen(g,4,0,1), b=gen(g,500,0,1); auto r=var_compare(a,b,p);
      printf("few      : verdict=%d note=%s\n",r.verdict,r.note);
      CHECK(r.verdict==VAR_UNDETERMINED); }
    // 6. 定数
    { std::vector<double> a(20,5.0); auto b=gen(g,20,5,1); auto r=var_compare(a,b,p);
      printf("constant : verdict=%d note=%s\n",r.verdict,r.note);
      CHECK(r.verdict==VAR_UNDETERMINED); CHECK(!std::isfinite(r.fold)); }
    // 7. NAN は欠測として無視(0 と数えない)
    { auto a=gen(g,100,10,1); auto a2=a; for(int i=0;i<100;i++)a2.push_back(NAN);
      auto s1=var_stats(a), s2=var_stats(a2);
      CHECK(s1.n==s2.n); CHECK(std::fabs(s1.sd-s2.sd)<1e-12);
      printf("nan skip : n=%d sd=%.4f\n",s2.n,s2.sd); }
    // 8. 倍率が閾値以上でも、少数で外れ値だけ → LARGE_NS になりうる(有意でない場合)
    { std::vector<double> a={1,2,3,4,5,6,7,8}, b={1,2,3,4,5,6,7,8}; b[7]=20;
      auto r=var_compare(a,b,p);
      printf("outlier  : fold=%.3f p=%.3f verdict=%d\n",r.fold,r.pval,r.verdict); }
    // 10. 相関(散布図の注記用)
    {
        std::mt19937 h(3); std::normal_distribution<double> nd(0,1);
        std::vector<double> x(300),lin(300),sq(300),ex(300),neg(300),noise(300);
        for(int i=0;i<300;i++){ x[i]=nd(h); lin[i]=2*x[i]+0.1*nd(h); sq[i]=x[i]*x[i]; ex[i]=std::exp(2*x[i]); neg[i]=-3*x[i]+1; noise[i]=nd(h); }
        printf("corr     : lin r=%.3f rho=%.3f | x^2 r=%.3f rho=%.3f | exp r=%.3f rho=%.3f | neg r=%.3f | noise r=%.3f\n",
               pearson_r(x,lin),spearman_rho(x,lin),pearson_r(x,sq),spearman_rho(x,sq),pearson_r(x,ex),spearman_rho(x,ex),pearson_r(x,neg),pearson_r(x,noise));
        CHECK(pearson_r(x,lin)>0.99 && spearman_rho(x,lin)>0.99);
        CHECK(std::fabs(pearson_r(x,sq))<0.15 && std::fabs(spearman_rho(x,sq))<0.15);   // 放物線は相関ほぼ 0
        CHECK(spearman_rho(x,ex)>0.9999 && pearson_r(x,ex)<0.95);                       // 単調だが直線でない: rho は 1、r は小さい
        CHECK(pearson_r(x,neg)<-0.9999);
        CHECK(std::fabs(pearson_r(x,noise))<0.2);
        std::vector<double> c(10,1.0), y10={1,2,3,4,5,6,7,8,9,10};
        CHECK(!std::isfinite(pearson_r(c,y10)));                                         // 定数は NAN
        CHECK(!std::isfinite(pearson_r({1,2},{1,2})));                                   // 2 組以下は NAN
        // NAN の組は除く
        std::vector<double> xn={1,2,NAN,4,5,6}, yn={2,4,6,NAN,10,12};
        CHECK(pearson_r(xn,yn)>0.9999);
        // 同順位(タイ)は平均順位: x の 1,1,2,2 と y の 1,2,3,4 → 手計算
        std::vector<double> tx={1,1,2,2}, ty={1,2,3,4};
        double rho = spearman_rho(tx,ty);
        printf("ties     : rho=%.6f (expected 0.894427)\n",rho);
        CHECK(std::fabs(rho-0.894427191)<1e-6);
    }
    // 9. 外部の値(scipy)との比較用: 固定データを出力
    if(argc>1){
        std::mt19937 h(7); auto a=gen(h,40,0,1), b=gen(h,25,0,2);
        printf("BF_P %.10g\n",brown_forsythe_p(a,b));
        FILE*f=fopen(argv[1],"w"); for(double x:a)fprintf(f,"%.17g ",x); fprintf(f,"\n"); for(double x:b)fprintf(f,"%.17g ",x); fprintf(f,"\n"); fclose(f);
    }
    printf(fails? "FAILED %d\n":"ALL OK\n",fails);
    return fails?1:0;
}
