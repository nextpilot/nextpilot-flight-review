#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::gui
{

// 画布的视图状态：可见范围的历史、图型、时间刻度、标题覆盖、轴变换配方。
// 全部是"视图自己的事"，与曲线数据（SeriesStore）无关。
class ViewState : public QObject
{
    Q_OBJECT // NOLINT
public:
    struct Range
    {
        double xLo = 0.0;
        double xHi = 1.0;
        double yLo = 0.0;
        double yHi = 1.0;
        bool equals(const Range &o) const;
    };

    explicit ViewState(QObject *parent = nullptr);

    // 初始视图作为撤销基线。
    void resetBaseline(const Range &r);
    // 记录一次变化：把"变化前"的基线压栈，然后基线跟到当前。
    void record(const Range &current);
    void setBaseline(const Range &r);
    bool canUndo() const;
    // 弹出上一段范围；取不到返回 false。
    bool takeUndo(Range *out);
    int historySize() const;
    bool restoring() const;
    void setRestoring(bool on);

    QString timeFormat() const;
    void setTimeFormat(const QString &mode); // "relative" / "hms" / "utc"

    QString titleOverride() const;
    void setTitleOverride(const QString &title);

    QString plotKind() const;
    void setPlotKind(const QString &kind);

    void setAxisTransform(const QString &axis, const QString &descriptorId);
    QString axisTransform(const QString &axis) const;
    void clearAxisTransforms();
    void replaceAxisTransforms(const QHash<QString, QString> &map);
    const QHash<QString, QString> &axisTransforms() const { return m_axisTransforms; }

signals:
    void timeFormatChanged(const QString &mode);
    void titleOverrideChanged(const QString &title);

private:
    QVector<Range> m_history;
    Range m_baseline;
    bool m_restoring = false;
    QString m_timeFormat = QStringLiteral("relative");
    QString m_titleOverride;
    QString m_plotKind = QStringLiteral("timeseries");
    QHash<QString, QString> m_axisTransforms;
};

} // namespace nextpilot::plotscope::gui
