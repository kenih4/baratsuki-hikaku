#ifndef VARIATION_H
#define VARIATION_H
#include <vector>
#include <cmath>

// バラつきの指標と、2 つの期間(A, B)の比較。Qt にも DB にも依存しない(単体で試験できる)。
// 入力の NAN / inf は無視する(欠測は 0 と数えない)。

struct VarStats{
    int n = 0;                  // 有限な値の個数
    double mean = NAN;
    double sd = NAN;            // 標本標準偏差(n-1)。n<2 なら NAN
    double cv = NAN;            // sd / |mean|。平均が 0 なら NAN
    double median = NAN;
    double mad = NAN;           // median(|x - median|)
    double iqr = NAN;           // 第3四分位 - 第1四分位(線形補間)
};

VarStats var_stats(const std::vector<double> &v);

// 判定の結果。小さい番号ほど目立たせる
enum VarVerdict{
    VAR_LARGE = 0,              // 倍率が閾値以上で、検定でも有意
    VAR_LARGE_NS = 1,           // 倍率は閾値以上だが、検定では有意でない(データが少ない/外れ値の影響など)
    VAR_SAME = 2,               // 倍率が閾値未満
    VAR_UNDETERMINED = 3        // データ不足、または定数で比べられない
};

struct VarParams{
    double threshold = 2.0;     // 「差が大きい」とする倍率
    double alpha = 0.05;        // 検定の有意水準
    int min_n = 5;              // A, B とも、これ未満の個数なら判定しない
};

struct VarCompare{
    VarStats a,b;
    VarVerdict verdict = VAR_UNDETERMINED;
    double ratio = NAN;         // sd_B / sd_A
    double fold = NAN;          // max(ratio, 1/ratio)。1 以上。大きいほどバラつきが違う
    double iqr_ratio = NAN;     // iqr_B / iqr_A(外れ値に強い参考値)
    double cv_ratio = NAN;      // cv_B / cv_A(平均に対する相対値の参考値)
    double pval = NAN;          // 等分散の検定(Brown–Forsythe)の p 値
    const char *note = "";      // VAR_UNDETERMINED の理由
};

// 等分散の検定 Brown–Forsythe(Levene の中央値版)の p 値。正規分布を仮定しない。
// どちらかが 2 個未満、または全体が 3 個未満なら NAN
double brown_forsythe_p(const std::vector<double> &a,const std::vector<double> &b);

VarCompare var_compare(const std::vector<double> &a,const std::vector<double> &b,const VarParams &p);

#endif // VARIATION_H
