#include "period_args.h"
#include <QRegularExpression>
#include <cmath>

bool parse_period_time(const QString &in,QDateTime &out,QString &err)
{
    QString s = in.trimmed();
    if(s.compare("now",Qt::CaseInsensitive)==0){
        out = QDateTime::currentDateTime();
        return true;
    }
    static const char *formats[] = {
        "yyyy/MM/dd HH:mm:ss","yyyy/MM/dd HH:mm",
        "yyyy-MM-dd HH:mm:ss","yyyy-MM-dd HH:mm",
        "yyyy-MM-ddTHH:mm:ss","yyyy-MM-ddTHH:mm"
    };
    for(const char *f : formats){
        QDateTime dt = QDateTime::fromString(s,f);     // ローカル時間として読む(画面の日時と同じ)
        if(dt.isValid()){
            out = dt;
            return true;
        }
    }
    err = QString("'%1' is not a date-time (use 'yyyy/MM/dd HH:mm:ss', 'yyyy-MM-dd HH:mm' or 'now')").arg(in);
    return false;
}

bool parse_period_width(const QString &in,int &value,qint64 &unit_sec,QString &err)
{
    static const QRegularExpression re("^(\\d+(?:\\.\\d+)?)\\s*([mhdMHD]?)$");
    QRegularExpressionMatch m = re.match(in.trimmed());
    if(!m.hasMatch()){
        err = QString("'%1' is not a width (use a number with m, h or d: e.g. 90, 30m, 2h, 7d)").arg(in);
        return false;
    }
    const QString u = m.captured(2).toLower();
    qint64 unit = u=="h" ? 3600 : (u=="d" ? 86400 : 60);      // 単位を省くと分
    bool ok = false;
    double v = m.captured(1).toDouble(&ok);
    if(!ok || !(v>0)){
        err = QString("width '%1' must be greater than 0").arg(in);
        return false;
    }
    const int MAX_VALUE = 100000;                              // 画面の入力欄の上限
    double r = std::round(v);
    if(std::fabs(v-r)<1e-9 && r<=MAX_VALUE){
        value = (int)r;                                        // 整数ならそのままの単位で
        unit_sec = unit;
        return true;
    }
    double minutes = v*unit/60.0;
    double rm = std::round(minutes);
    if(std::fabs(minutes-rm)>1e-6){
        err = QString("width '%1' is not a whole number of minutes").arg(in);
        return false;
    }
    if(rm>MAX_VALUE || rm<1){
        err = QString("width '%1' is out of range (1 to %2 minutes or the same count of hours/days)").arg(in).arg(MAX_VALUE);
        return false;
    }
    value = (int)rm;
    unit_sec = 60;
    return true;
}
