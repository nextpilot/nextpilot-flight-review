#pragma once

#include "fastplot/Instrument.h"

namespace fastplot
{

class IPlotData;

// 经纬轨迹图（老画布的 "GPS Map"）。
//
// 名字不叫 Map：它没有底图、没有瓦片、没有墨卡托投影，只有经纬度点连成的折线
// + 经纬网格。叫 Track 才名实相符，将来真接底图时再换实现，不用改调用方语义。
class TrackPlot : public Instrument
{
    Q_OBJECT
public:
    explicit TrackPlot(QWidget *parent = nullptr);

    // 非拥有：直读调用方的数组，不复制。传 nullptr 表示没有轨迹。
    void setTrack(const IPlotData *longitude, const IPlotData *latitude,
                  const QString &lonLabel = QString(), const QString &latLabel = QString());
    void clearTrack();

    void setStartColor(const QColor &color);
    void setEndColor(const QColor &color);

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    const IPlotData *m_lon = nullptr;
    const IPlotData *m_lat = nullptr;
    QString m_lonLabel;
    QString m_latLabel;
    QColor m_start{0x3d, 0xdc, 0x84};
    QColor m_end{0xff, 0x6b, 0x6b};
};

} // namespace fastplot
