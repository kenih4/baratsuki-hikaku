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
    // 9. 外部の値(scipy)との比較用: 固定データを出力
    if(argc>1){
        std::mt19937 h(7); auto a=gen(h,40,0,1), b=gen(h,25,0,2);
        printf("BF_P %.10g\n",brown_forsythe_p(a,b));
        FILE*f=fopen(argv[1],"w"); for(double x:a)fprintf(f,"%.17g ",x); fprintf(f,"\n"); for(double x:b)fprintf(f,"%.17g ",x); fprintf(f,"\n"); fclose(f);
    }
    printf(fails? "FAILED %d\n":"ALL OK\n",fails);
    return fails?1:0;
}
