#include "MainWindow.h"
#include "DialogEngine.h"
#include "DataSourceDialog.h"
#include "DatasetCache.h"
#include "DataSourceRegistry.h"
#include "Component.h"
#include "LiveSession.h"
#include "DataSourceRegistry.h"

#include <QAction>
#include <QtEndian>
#include <QTreeWidgetItemIterator>
#include <cstring>
#include <algorithm>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QDir>
#include <QStandardPaths>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QWidgetAction>
#include <QMouseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QDateTime>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QResizeEvent>
#include <QStyle>
#include <QHash>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QPixmap>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>

static QString dockIconResource(const QDockWidget *d);
static void execDockMenu(QToolButton *btn, QMenu *menu);

#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QLineEdit>
#include <QEvent>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include <QListWidget>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "CanvasWidget.h"
#include "DataSourceDock.h"
#include "PlotWorkspace.h"
#include "ScriptService.h"
#include "ps_kernel.h"
#include "TransformDialogInterface.h"

namespace {

// Ribbon 里的图表选择卡：用缩略图表达图型，比一排纯文字按钮更接近 Office/MATLAB 的样式库
void compactValueTable(QTableWidget *t)
{
    if (!t)
        return;
    t->setAlternatingRowColors(false);
    t->setWordWrap(false);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(21);
    t->verticalHeader()->setMinimumSectionSize(21);
    t->horizontalHeader()->setFixedHeight(23);
    QFont f = t->font();
    f.setPixelSize(12);
    t->setFont(f);
}

QPixmap plotThumb(const QString &kind)
{
    QPixmap pm(76, 38);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#2d3841"), 1));
    p.setBrush(QColor("#0b0f13"));
    p.drawRoundedRect(QRectF(0.5, 0.5, 75, 37), 3, 3);
    const QRectF r(7, 6, 62, 26);
    p.setPen(QPen(QColor("#26313a"), 1));
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.topLeft());

    if (kind == QLatin1String("timeseries"))
    {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i)
        {
            const double x = r.left() + r.width() * i / 40.0;
            const double y = r.center().y() - std::sin(i * 0.28) * 8.0;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(QColor("#4ea1ff"), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("xy"))
    {
        for (int i = 0; i < 9; ++i)
        {
            const double x = r.left() + 6 + (i * 13) % 50;
            const double y = r.bottom() - 3 - (i * 7) % 20;
            p.setPen(QPen(QColor("#ffb454"), 1));
            p.setBrush(QColor("#ffb454"));
            p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
    }
    else if (kind == QLatin1String("scatter"))
    {
        for (int i = 0; i < 14; ++i)
        {
            const double x = r.left() + 5 + (i * 17) % 52;
            const double y = r.bottom() - 2 - (i * 11) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(i % 3 ? QColor("#7ee787") : QColor("#ffb454"));
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
    }
    else if (kind == QLatin1String("map"))
    {
        p.setPen(QPen(QColor("#26313a"), 1));
        for (int i = 1; i < 4; ++i)
        {
            p.drawLine(QPointF(r.left() + r.width() * i / 4.0, r.top()),
                       QPointF(r.left() + r.width() * i / 4.0, r.bottom()));
            p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4.0),
                       QPointF(r.right(), r.top() + r.height() * i / 4.0));
        }
        QPainterPath path;
        path.moveTo(r.left() + 6, r.bottom() - 4);
        path.cubicTo(r.left() + 20, r.top() + 2, r.left() + 34, r.bottom() - 6, r.right() - 4, r.top() + 6);
        p.setPen(QPen(QColor("#4ea1ff"), 1.5));
        p.drawPath(path);
        p.setBrush(QColor("#ff7b72"));
        p.drawEllipse(QPointF(r.right() - 4, r.top() + 6), 2.2, 2.2);
    }
    else if (kind == QLatin1String("bar"))
    {
        for (int i = 0; i < 5; ++i)
        {
            const double h = 4 + (i * 7) % 19;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#7ee787"));
            p.drawRect(QRectF(r.left() + 5 + i * 11, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("histogram"))
    {
        for (int i = 0; i < 7; ++i)
        {
            const double h = 3 + (i * 5 + i * i) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#d2a8ff"));
            p.drawRect(QRectF(r.left() + 4 + i * 8, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("fft"))
    {
        QPainterPath path;
        for (int i = 0; i <= 32; ++i)
        {
            const double x = r.left() + r.width() * i / 32.0;
            const double env = std::exp(-i * 0.12);
            const double y = r.bottom() - 2 - std::fabs(std::sin(i * 0.55)) * 18.0 * env;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(QColor("#39c5cf"), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("traj3d"))
    {
        p.setPen(QPen(QColor("#26313a"), 1));
        p.drawLine(QPointF(12, 28), QPointF(32, 21));
        p.drawLine(QPointF(32, 21), QPointF(58, 27));
        p.drawLine(QPointF(58, 27), QPointF(58, 9));
        p.drawLine(QPointF(58, 9), QPointF(36, 8));
        p.drawLine(QPointF(36, 8), QPointF(12, 14));
        p.drawLine(QPointF(12, 14), QPointF(12, 28));
        QPainterPath path;
        path.moveTo(14, 25);
        path.cubicTo(24, 8, 40, 12, 54, 20);
        p.setPen(QPen(QColor("#f2b134"), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("surface3d"))
    {
        p.setPen(QPen(QColor("#2a3b48"), 1));
        for (int i = 0; i < 6; ++i)
        {
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(22 + i * 9, 31));
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(34 + i * 9, 9));
        }
        QPainterPath path;
        path.moveTo(12, 22);
        path.cubicTo(26, 8, 44, 27, 64, 13);
        p.setPen(QPen(QColor("#39c5cf"), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("horizon"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1));
        p.setBrush(QColor("#2b3a4a"));
        p.drawEllipse(QPointF(38, 19), 14, 14);
        p.setBrush(QColor("#3a2f22"));
        p.drawChord(QRectF(24, 5, 28, 28), 180 * 16, 180 * 16);
        p.setPen(QPen(QColor("#f2b134"), 1.4));
        p.drawLine(QPointF(25, 19), QPointF(51, 19));
        p.drawLine(QPointF(38, 17), QPointF(38, 21));
        p.drawLine(QPointF(31, 19), QPointF(45, 19));
    }
    else if (kind == QLatin1String("compass"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1));
        p.drawEllipse(QPointF(38, 19), 13, 13);
        p.drawText(QRectF(35, 5, 8, 8), Qt::AlignCenter, QStringLiteral("N"));
        p.setPen(QPen(QColor("#ff7b72"), 1.8));
        p.drawLine(QPointF(38, 19), QPointF(43, 10));
        p.setPen(QPen(QColor("#c9d1d9"), 1));
        p.drawLine(QPointF(38, 19), QPointF(34, 27));
    }
    else if (kind == QLatin1String("gauge"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1.2));
        p.drawArc(QRectF(18, 7, 40, 34), 200 * 16, -220 * 16);
        p.setPen(QPen(QColor("#f2b134"), 1.6));
        p.drawLine(QPointF(38, 23), QPointF(51, 13));
        p.setBrush(QColor("#f2b134"));
        p.drawEllipse(QPointF(38, 23), 2, 2);
    }
    return pm;
}

QPixmap sourceThumb(const QString &kind)
{
    QPixmap pm(76, 38);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#2d3841"), 1));
    p.setBrush(QColor("#0b0f13"));
    p.drawRoundedRect(QRectF(0.5, 0.5, 75, 37), 3, 3);
    const QColor accent = kind == QLatin1String("ulog")     ? QColor("#4ea1ff")
                       : kind == QLatin1String("bin")      ? QColor("#ffb454")
                       : kind == QLatin1String("csv")      ? QColor("#7ee787")
                       : kind == QLatin1String("can")      ? QColor("#d2a8ff")
                       : kind == QLatin1String("mavlink")  ? QColor("#39c5cf")
                       : kind == QLatin1String("ros2")     ? QColor("#ff7b72")
                                                          : QColor("#f2b134");
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRect(QRectF(0, 0, 3, 38));
    p.setPen(QPen(accent, 1.2));
    QPainterPath path;
    for (int i = 0; i <= 28; ++i)
    {
        const double x = 10 + i * 2.0;
        const double y = 19 - std::sin(i * 0.42) * (kind == QLatin1String("can") ? 6.0 : 8.0);
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    p.drawPath(path);
    p.setPen(QColor("#c9d1d9"));
    QFont f = p.font();
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(9, 3, 60, 10), Qt::AlignLeft | Qt::AlignVCenter,
               kind == QLatin1String("ulog") ? QStringLiteral("ULog")
               : kind == QLatin1String("bin") ? QStringLiteral("BIN")
               : kind == QLatin1String("csv") ? QStringLiteral("CSV")
               : kind == QLatin1String("can") ? QStringLiteral("CAN")
               : kind == QLatin1String("mavlink") ? QStringLiteral("MAV")
               : kind == QLatin1String("ros2") ? QStringLiteral("ROS2")
                                               : QStringLiteral("RT"));
    return pm;
}

class SeriesEditDialog : public QDialog
{
public:
    SeriesEditDialog(const QJsonObject &st, const QVector<const PsChannelInfo *> &channels,
                     const QString &alias, QWidget *parent = nullptr)
        : QDialog(parent), m_channels(channels), m_alias(alias)
    {
        setWindowTitle(QStringLiteral("曲线属性"));
        resize(430, 340);
        m_color = QColor(st.value(QStringLiteral("color")).toString());
        if (!m_color.isValid())
            m_color = QColor(QStringLiteral("#4ea1ff"));

        auto *root = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this);

        // 曲线：线型 / 颜色 / 线宽 / 轴 / 可见
        auto *curvePage = new QWidget(tabs);
        auto *curveForm = new QFormLayout(curvePage);
        m_label = new QLineEdit(st.value(QStringLiteral("label")).toString(), curvePage);
        curveForm->addRow(QStringLiteral("图例"), m_label);
        m_colorBtn = new QPushButton(curvePage);
        m_colorBtn->setFixedHeight(24);
        setColorButton();
        connect(m_colorBtn, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_color, this, QStringLiteral("曲线颜色"));
            if (c.isValid())
            {
                m_color = c;
                setColorButton();
            }
        });
        curveForm->addRow(QStringLiteral("颜色"), m_colorBtn);
        m_width = new QSpinBox(curvePage);
        m_width->setRange(1, 6);
        m_width->setSuffix(QStringLiteral(" px"));
        m_width->setValue(st.value(QStringLiteral("width")).toInt(1));
        curveForm->addRow(QStringLiteral("线宽"), m_width);
        m_style = new QComboBox(curvePage);
        m_style->addItem(QStringLiteral("实线"), 0);
        m_style->addItem(QStringLiteral("虚线"), 1);
        m_style->addItem(QStringLiteral("点线"), 2);
        m_style->addItem(QStringLiteral("点划线"), 3);
        m_style->setCurrentIndex(m_style->findData(st.value(QStringLiteral("style")).toInt(0)));
        curveForm->addRow(QStringLiteral("线型"), m_style);
        m_marker = new QComboBox(curvePage);
        m_marker->addItem(QStringLiteral("无"), 0);
        m_marker->addItem(QStringLiteral("圆点"), 1);
        m_marker->addItem(QStringLiteral("方块"), 2);
        m_marker->addItem(QStringLiteral("菱形"), 3);
        m_marker->addItem(QStringLiteral("三角"), 4);
        m_marker->addItem(QStringLiteral("十字"), 5);
        m_marker->addItem(QStringLiteral("叉号"), 6);
        m_marker->setCurrentIndex(m_marker->findData(st.value(QStringLiteral("marker")).toInt(0)));
        curveForm->addRow(QStringLiteral("标记"), m_marker);
        m_markerSize = new QSpinBox(curvePage);
        m_markerSize->setRange(1, 12);
        m_markerSize->setSuffix(QStringLiteral(" px"));
        m_markerSize->setValue(st.value(QStringLiteral("markerSize")).toInt(5));
        curveForm->addRow(QStringLiteral("标记大小"), m_markerSize);
        m_axis = new QComboBox(curvePage);
        m_axis->addItem(QStringLiteral("左 Y 轴"), 0);
        m_axis->addItem(QStringLiteral("右 Y 轴"), 1);
        m_axis->setCurrentIndex(st.value(QStringLiteral("axis")).toInt(0) == 1 ? 1 : 0);
        curveForm->addRow(QStringLiteral("绑定轴"), m_axis);
        m_visible = new QCheckBox(QStringLiteral("显示该曲线"), curvePage);
        m_visible->setChecked(st.value(QStringLiteral("visible")).toBool(true));
        curveForm->addRow(QStringLiteral("可见"), m_visible);
        tabs->addTab(curvePage, QStringLiteral("曲线"));

        // X 轴：时间偏移
        auto *xPage = new QWidget(tabs);
        auto *xForm = new QFormLayout(xPage);
        m_offset = new QDoubleSpinBox(xPage);
        m_offset->setRange(-1e6, 1e6);
        m_offset->setDecimals(6);
        m_offset->setSuffix(QStringLiteral(" s"));
        m_offset->setValue(st.value(QStringLiteral("offset")).toDouble(0));
        xForm->addRow(QStringLiteral("时间偏移"), m_offset);
        auto *xNote = new QLabel(QStringLiteral("显示时间 = 原始时间 − 偏移；只改显示，不改原始数据。"), xPage);
        xNote->setWordWrap(true);
        xNote->setStyleSheet(QStringLiteral("color:#8494a0;"));
        xForm->addRow(xNote);
        tabs->addTab(xPage, QStringLiteral("X 轴"));

        // Y 轴：换数据列 / 算子 / 脚本
        auto *yPage = new QWidget(tabs);
        auto *yForm = new QFormLayout(yPage);
        m_column = new QComboBox(yPage);
        const QString curName = st.value(QStringLiteral("name")).toString();
        for (const PsChannelInfo *ch : m_channels)
            if (ch)
                m_column->addItem(QString::fromUtf8(ch->name),
                                  QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        const int ci = m_column->findText(curName);
        if (ci >= 0)
            m_column->setCurrentIndex(ci);
        yForm->addRow(QStringLiteral("数据列"), m_column);
        m_transform = new QComboBox(yPage);
        m_transform->addItem(QStringLiteral("原值（无变换）"), 0);
        m_transform->addItem(QStringLiteral("微分 dv/dt"), 1);
        m_transform->addItem(QStringLiteral("梯形积分"), 2);
        m_transform->addItem(QStringLiteral("滑动平均"), 3);
        m_transform->addItem(QStringLiteral("绝对值"), 4);
        m_transform->addItem(QStringLiteral("归一化 0–1"), 5);
        m_transform->addItem(QStringLiteral("Lua 脚本"), 6);
        yForm->addRow(QStringLiteral("变换"), m_transform);
        m_window = new QSpinBox(yPage);
        m_window->setRange(2, 500);
        m_window->setValue(5);
        yForm->addRow(QStringLiteral("窗口"), m_window);
        m_script = new QPlainTextEdit(yPage);
        m_script->setPlaceholderText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setPlainText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setFixedHeight(78);
        yForm->addRow(QStringLiteral("脚本"), m_script);
        auto *yNote = new QLabel(QStringLiteral("算子/脚本会把结果写成派生曲线；脚本输入为 ps.input(1)，输出到 out。"), yPage);
        yNote->setWordWrap(true);
        yNote->setStyleSheet(QStringLiteral("color:#8494a0;"));
        yForm->addRow(yNote);
        connect(m_transform, &QComboBox::currentIndexChanged, this, [this] {
            const int id = m_transform->currentData().toInt();
            m_window->setEnabled(id == 3);
            m_script->setEnabled(id == 6);
        });
        m_window->setEnabled(false);
        m_script->setEnabled(false);
        tabs->addTab(yPage, QStringLiteral("Y 轴"));

        root->addWidget(tabs);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(bb);
    }

    QJsonObject patch() const
    {
        QJsonObject o;
        o[QStringLiteral("label")] = m_label->text().trimmed();
        o[QStringLiteral("color")] = m_color.name();
        o[QStringLiteral("width")] = m_width->value();
        o[QStringLiteral("style")] = m_style->currentData().toInt();
        o[QStringLiteral("marker")] = m_marker->currentData().toInt();
        o[QStringLiteral("markerSize")] = m_markerSize->value();
        o[QStringLiteral("axis")] = m_axis->currentData().toInt();
        o[QStringLiteral("visible")] = m_visible->isChecked();
        o[QStringLiteral("offset")] = m_offset->value();
        return o;
    }
    int transformId() const { return m_transform->currentData().toInt(); }
    int windowSize() const { return m_window->value(); }
    QString scriptText() const { return m_script->toPlainText(); }
    const PsChannelInfo *channel() const
    {
        const QVariant v = m_column->currentData();
        return v.isValid() ? reinterpret_cast<const PsChannelInfo *>(v.toULongLong()) : nullptr;
    }
    QString channelName() const { return m_column->currentText(); }
    QString alias() const { return m_alias; }

private:
    void setColorButton()
    {
        m_colorBtn->setText(m_color.name());
        m_colorBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:#0d1117;border:1px solid #3a4854;border-radius:2px;"
            "font-weight:600;}QPushButton:hover{border-color:#f2b134;}").arg(m_color.name()));
    }

    QVector<const PsChannelInfo *> m_channels;
    QString m_alias;
    QColor m_color;
    QLineEdit *m_label = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QSpinBox *m_width = nullptr;
    QComboBox *m_style = nullptr;
    QComboBox *m_marker = nullptr;
    QSpinBox *m_markerSize = nullptr;
    QComboBox *m_axis = nullptr;
    QCheckBox *m_visible = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QComboBox *m_column = nullptr;
    QComboBox *m_transform = nullptr;
    QSpinBox *m_window = nullptr;
    QPlainTextEdit *m_script = nullptr;
};

class AxesEditDialog : public QDialog
{
public:
    AxesEditDialog(const QJsonObject &st, QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("画布属性"));
        resize(440, 500);
        m_axisColor = QColor(st.value(QStringLiteral("axisColor")).toString());
        auto *root = new QVBoxLayout(this);
        auto *form = new QFormLayout;

        m_title = new QLineEdit(st.value(QStringLiteral("title")).toString(), this);
        form->addRow(QStringLiteral("标题 Title"), m_title);
        m_xLabel = new QLineEdit(st.value(QStringLiteral("xlabel")).toString(), this);
        m_xLabel->setPlaceholderText(QStringLiteral("time / s"));
        form->addRow(QStringLiteral("X 轴标签 XLabel"), m_xLabel);
        m_yLabel = new QLineEdit(st.value(QStringLiteral("ylabel")).toString(), this);
        m_yLabel->setPlaceholderText(QStringLiteral("value"));
        form->addRow(QStringLiteral("Y 轴标签 YLabel"), m_yLabel);

        m_gridX = new QCheckBox(QStringLiteral("X 方向网格"), this);
        m_gridX->setChecked(st.value(QStringLiteral("gridX")).toBool(st.value(QStringLiteral("grid")).toBool(true)));
        form->addRow(QStringLiteral("XGrid"), m_gridX);
        m_gridY = new QCheckBox(QStringLiteral("Y 方向网格"), this);
        m_gridY->setChecked(st.value(QStringLiteral("gridY")).toBool(st.value(QStringLiteral("grid")).toBool(true)));
        form->addRow(QStringLiteral("YGrid"), m_gridY);
        m_minorGrid = new QCheckBox(QStringLiteral("次网格（虚线）"), this);
        m_minorGrid->setChecked(st.value(QStringLiteral("minorGrid")).toBool(false));
        form->addRow(QStringLiteral("MinorGrid"), m_minorGrid);
        m_box = new QCheckBox(QStringLiteral("显示边框"), this);
        m_box->setChecked(st.value(QStringLiteral("box")).toBool(true));
        form->addRow(QStringLiteral("Box"), m_box);
        m_flipY = new QCheckBox(QStringLiteral("翻转 Y 轴方向"), this);
        m_flipY->setChecked(st.value(QStringLiteral("flipY")).toBool(false));
        form->addRow(QStringLiteral("YDir / FlipY"), m_flipY);
        m_legend = new QCheckBox(QStringLiteral("显示图例"), this);
        m_legend->setChecked(st.value(QStringLiteral("legend")).toBool(true));
        form->addRow(QStringLiteral("Legend"), m_legend);
        m_legendPos = new QComboBox(this);
        m_legendPos->addItem(QStringLiteral("左上"), 0);
        m_legendPos->addItem(QStringLiteral("右上"), 1);
        m_legendPos->addItem(QStringLiteral("左下"), 2);
        m_legendPos->addItem(QStringLiteral("右下"), 3);
        m_legendPos->setCurrentIndex(st.value(QStringLiteral("legendPos")).toInt(0));
        form->addRow(QStringLiteral("图例位置"), m_legendPos);

        m_fontSize = new QSpinBox(this);
        m_fontSize->setRange(7, 20);
        m_fontSize->setSuffix(QStringLiteral(" px"));
        m_fontSize->setValue(st.value(QStringLiteral("axisFontSize")).toInt(11));
        form->addRow(QStringLiteral("FontSize"), m_fontSize);
        m_lineWidth = new QSpinBox(this);
        m_lineWidth->setRange(1, 4);
        m_lineWidth->setSuffix(QStringLiteral(" px"));
        m_lineWidth->setValue(st.value(QStringLiteral("axisLineWidth")).toInt(1));
        form->addRow(QStringLiteral("轴线宽 LineWidth"), m_lineWidth);
        m_colorBtn = new QPushButton(this);
        m_colorBtn->setFixedHeight(24);
        setColorButton();
        connect(m_colorBtn, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_axisColor.isValid() ? m_axisColor : QColor("#8b949e"),
                                                    this, QStringLiteral("坐标轴颜色"));
            if (c.isValid())
            {
                m_axisColor = c;
                setColorButton();
            }
        });
        form->addRow(QStringLiteral("轴线颜色"), m_colorBtn);

        root->addLayout(form);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(bb);
    }
    QString title() const { return m_title->text().trimmed(); }
    QString xLabel() const { return m_xLabel->text().trimmed(); }
    QString yLabel() const { return m_yLabel->text().trimmed(); }
    bool gridX() const { return m_gridX->isChecked(); }
    bool gridY() const { return m_gridY->isChecked(); }
    bool minorGrid() const { return m_minorGrid->isChecked(); }
    bool box() const { return m_box->isChecked(); }
    bool flipY() const { return m_flipY->isChecked(); }
    bool legend() const { return m_legend->isChecked(); }
    int legendPosition() const { return m_legendPos->currentData().toInt(); }
    int fontSize() const { return m_fontSize->value(); }
    int lineWidth() const { return m_lineWidth->value(); }
    QColor axisColorValue() const { return m_axisColor; }

private:
    void setColorButton()
    {
        m_colorBtn->setText(m_axisColor.isValid() ? m_axisColor.name() : QStringLiteral("跟随主题"));
        if (m_axisColor.isValid())
            m_colorBtn->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;color:#0d1117;border:1px solid #3a4854;border-radius:2px;"
                "font-weight:600;}QPushButton:hover{border-color:#f2b134;}").arg(m_axisColor.name()));
    }

    QLineEdit *m_title = nullptr;
    QLineEdit *m_xLabel = nullptr;
    QLineEdit *m_yLabel = nullptr;
    QCheckBox *m_gridX = nullptr;
    QCheckBox *m_gridY = nullptr;
    QCheckBox *m_minorGrid = nullptr;
    QCheckBox *m_box = nullptr;
    QCheckBox *m_flipY = nullptr;
    QCheckBox *m_legend = nullptr;
    QComboBox *m_legendPos = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QSpinBox *m_lineWidth = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QColor m_axisColor;
};

} // namespace
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

static void darkenDocks(MainWindow *w);
namespace { void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area); }

// 只重写 sizeHint 的标题栏：QDockWidget 按 sizeHint 预留高度，固定 height 不算数，
// 32px 的 bar 只留 24px 空间就会压住正文第一行。

class DockTitleBar : public QWidget
{
public:
    explicit DockTitleBar(MainWindow *win, QDockWidget *dock)
        : QWidget(dock), m_win(win), m_dock(dock)
    {
        setFixedHeight(32);
        setCursor(Qt::SizeAllCursor);
        setToolTip(QStringLiteral("拖动到窗口边缘可停靠，双击切换浮动"));
    }
    QSize sizeHint() const override { return QSize(QWidget::sizeHint().width(), 32); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(e);
            return;
        }
        m_pressPos = e->globalPosition().toPoint();
        m_dragging = false;
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || !m_dock)
        {
            QWidget::mouseMoveEvent(e);
            return;
        }
        const QPoint gp = e->globalPosition().toPoint();
        if (!m_dragging && (gp - m_pressPos).manhattanLength() < 6)
            return;
        if (!m_dragging)
        {
            m_dragging = true;
            m_offset = gp - m_dock->frameGeometry().topLeft();
            if (!m_dock->isFloating())
                m_dock->setFloating(true);
        }
        m_dock->move(gp - m_offset);
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !m_dragging)
        {
            QWidget::mouseReleaseEvent(e);
            return;
        }
        m_dragging = false;
        if (!m_dock || !m_win)
            return;
        const QPoint gp = e->globalPosition().toPoint();
        const QRect winRect = m_win->frameGeometry();
        if (!winRect.adjusted(-24, -24, 24, 24).contains(gp))
            return; // 扔到窗口外：保持浮动
        const int dl = qAbs(gp.x() - winRect.left());
        const int dr = qAbs(gp.x() - winRect.right());
        const int db = qAbs(gp.y() - winRect.bottom());
        const int dt = qAbs(gp.y() - winRect.top());
        const int minD = std::min(std::min(dl, dr), std::min(db, dt));
        if (minD > 150)
            return; // 中间区域：保持浮动，不强行吸边
        Qt::DockWidgetArea area = dr <= dl ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
        if (db < std::min(dl, dr) && db <= dt)
            area = Qt::BottomDockWidgetArea;
        else if (dt < std::min(dl, dr) && dt < db)
            area = Qt::TopDockWidgetArea;
        dockToArea(m_dock, m_win, area);
        e->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_dock)
        {
            m_dock->setFloating(!m_dock->isFloating());
            e->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

private:
    MainWindow *m_win = nullptr;
    QDockWidget *m_dock = nullptr;
    QPoint m_pressPos;
    QPoint m_offset;
    bool m_dragging = false;
};

// Visual Studio 式自动隐藏页签：收成一条竖直窄条，标题竖排，点一下展开
class DockAutoHideTab : public QWidget
{
public:
    using Handler = std::function<void()>;

    DockAutoHideTab(QWidget *parent, const QString &title, Qt::DockWidgetArea area, Handler handler)
        : QWidget(parent), m_title(title), m_handler(std::move(handler))
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(QStringLiteral("展开 %1").arg(title));
        setArea(area);
    }

    void setArea(Qt::DockWidgetArea area)
    {
        m_area = area;
        const int h = std::clamp(48 + int(m_title.size()) * 13, 96, 200);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
            setFixedSize(26, h);
        else
            setFixedSize(h, 26);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(m_hover ? QColor("#1f2a33") : QColor("#161b20"));
        p.setPen(QPen(m_hover ? QColor("#f2b134") : QColor("#2d3841"), 1));
        p.drawRoundedRect(r, 2, 2);

        p.setPen(QColor("#f2b134"));
        QFont arrow = font();
        arrow.setPixelSize(12);
        p.setFont(arrow);
        const QString mark = (m_area == Qt::LeftDockWidgetArea) ? QStringLiteral("›")
                            : (m_area == Qt::RightDockWidgetArea) ? QStringLiteral("‹")
                            : (m_area == Qt::TopDockWidgetArea) ? QStringLiteral("▼") : QStringLiteral("▲");
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
            p.drawText(QRectF(0, 2, width(), 16), Qt::AlignCenter, mark);
        else
            p.drawText(QRectF(2, 0, 16, height()), Qt::AlignCenter, mark);

        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(QColor("#c9d1d9"));
        p.save();
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
        {
            p.translate(width() / 2.0, height() / 2.0);
            p.rotate(m_area == Qt::LeftDockWidgetArea ? 90 : -90);
            p.drawText(QRectF(-height() / 2.0 + 10, -width() / 2.0, height() - 16, width()),
                       Qt::AlignCenter, m_title);
        }
        else
        {
            p.drawText(QRectF(18, 0, width() - 22, height()), Qt::AlignCenter, m_title);
        }
        p.restore();
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_handler)
            m_handler();
    }

private:
    QString m_title;
    Handler m_handler;
    Qt::DockWidgetArea m_area = Qt::RightDockWidgetArea;
    bool m_hover = false;
};

namespace {

QHash<QDockWidget *, QWidget *> g_dockMinHandles;

void positionDockHandle(QDockWidget *dock, QWidget *btn, MainWindow *win, int index = 0, int total = 1)
{
    if (!dock || !btn || !win)
        return;
    const QRect cg = win->centralWidget()->geometry();
    const Qt::DockWidgetArea area = win->dockWidgetArea(dock);
    const int W = 26, GAP = 6;
    const int H = qMax(96, btn->height());
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int x = area == Qt::LeftDockWidgetArea ? cg.left() + 2 : cg.right() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int y = cg.center().y() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    else
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int y = area == Qt::TopDockWidgetArea ? cg.top() + 2 : cg.bottom() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int x = cg.center().x() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    btn->raise();
}

void repositionDockHandles(MainWindow *win)
{
    if (!win)
        return;
    QHash<int, int> totals, seen;
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
        if (it.value() && it.value()->isVisible())
            ++totals[static_cast<int>(win->dockWidgetArea(it.key()))];
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
    {
        if (!it.value() || !it.value()->isVisible())
            continue;
        const int area = static_cast<int>(win->dockWidgetArea(it.key()));
        const int idx = seen[area]++;
        positionDockHandle(it.key(), it.value(), win, idx, qMax(1, totals.value(area, 1)));
    }
}

void collapseDock(QDockWidget *dock, MainWindow *win)
{
    if (!dock || !win)
        return;
    QWidget *btn = g_dockMinHandles.value(dock);
    if (!btn)
    {
        auto *tab = new DockAutoHideTab(win, dock->windowTitle(), win->dockWidgetArea(dock),
            [dock, win] {
                dock->show();
                dock->raise();
                if (auto *h = g_dockMinHandles.value(dock))
                    h->hide();
                repositionDockHandles(win);
            });
        QObject::connect(dock, &QDockWidget::visibilityChanged, tab, [tab](bool visible) {
            if (visible)
                tab->hide();
        });
        g_dockMinHandles.insert(dock, tab);
        btn = tab;
    }
    dock->hide();
    btn->show();
    repositionDockHandles(win);
}

void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area)
{
    if (!dock || !win)
        return;
    const bool floating = dock->isFloating();
    if (floating)
        dock->setFloating(false);
    win->removeDockWidget(dock);
    win->addDockWidget(area, dock);
    dock->show();
    dock->raise();
}


// Word 插入表格式布局选择：悬停框选 m×n，点击即应用
class PlotMenuPopup : public QFrame
{
public:
    explicit PlotMenuPopup(QWidget *parent) : QFrame(parent, Qt::Popup)
    {
        setObjectName(QStringLiteral("plotMenuPopup"));
        setStyleSheet(QStringLiteral(
            "QFrame#plotMenuPopup{background:#1b232b;border:1px solid #3a4650;}"));
        m_root = new QVBoxLayout(this);
        m_root->setContentsMargins(8, 6, 8, 8);
        m_root->setSpacing(4);
    }

    void addGroup(const QString &title, const QList<QAction *> &acts)
    {
        auto *lab = new QLabel(title, this);
        lab->setStyleSheet(QStringLiteral("color:#8b949e;font-size:11px;padding:2px 3px;"));
        m_root->addWidget(lab);
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(3);
        for (int i = 0; i < acts.size(); ++i)
        {
            auto *b = new QToolButton(this);
            b->setDefaultAction(acts.at(i));
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setAutoRaise(true);
            b->setIconSize(QSize(64, 36));
            b->setFixedSize(70, 44);
            b->setStyleSheet(QStringLiteral(
                "QToolButton{background:transparent;border:0;padding:0;}"
                "QToolButton:hover{background:#2d3841;}"));
            connect(acts.at(i), &QAction::triggered, this, &QWidget::hide);
            grid->addWidget(b, i / 6, i % 6);
        }
        for (int c = 0; c < 6; ++c)
            grid->setColumnStretch(c, 0);
        m_root->addLayout(grid);
    }

private:
    QVBoxLayout *m_root;
};

class LayoutPickGrid : public QWidget
{
public:
    explicit LayoutPickGrid(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);
        setFixedSize(8 * kStep + 2 * kPad, 8 * kStep + 2 * kPad);
    }

    std::function<void(int, int)> onPick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter pr(this);
        pr.fillRect(rect(), QColor(27, 35, 43));
        for (int r = 1; r <= 8; ++r)
        {
            for (int c = 1; c <= 8; ++c)
            {
                const QRect cell(kPad + (c - 1) * kStep, kPad + (r - 1) * kStep, 16, 16);
                const bool on = r <= m_rows && c <= m_cols;
                pr.fillRect(cell, on ? QColor(242, 177, 52) : QColor(45, 56, 65));
                pr.setPen(on ? QColor(255, 214, 125) : QColor(70, 84, 96));
                pr.drawRect(cell.adjusted(0, 0, -1, -1));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int c = std::clamp((e->pos().x() - kPad) / kStep + 1, 0, 8);
        const int r = std::clamp((e->pos().y() - kPad) / kStep + 1, 0, 8);
        if (r != m_rows || c != m_cols)
        {
            m_rows = r;
            m_cols = c;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_rows = m_cols = 0;
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || m_rows < 1 || m_cols < 1)
            return;
        if (onPick)
            onPick(m_rows, m_cols);
    }

private:
    static constexpr int kPad = 4;
    static constexpr int kStep = 18;
    int m_rows = 0;
    int m_cols = 0;
};

// 页内 m×n 网格：QSplitter 只能做 n×1 / 1×n，这里补真正的二维布局
class PlotGrid : public QWidget
{
public:
    explicit PlotGrid(QWidget *parent = nullptr) : QWidget(parent), m_grid(new QGridLayout(this))
    {
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setSpacing(4);
    }

    void setShape(int rows, int cols)
    {
        m_rows = std::clamp(rows, 1, 8);
        m_cols = std::clamp(cols, 1, 8);
        while (m_canvases.size() > m_rows * m_cols)
            ++m_rows;
        relayout();
    }

    void addCanvas(CanvasWidget *c)
    {
        if (!c || m_canvases.contains(c))
            return;
        c->setParent(this);
        c->show();
        m_canvases.append(c);
        while (m_canvases.size() > m_rows * m_cols)
            ++m_rows;
        relayout();
    }

    void removeCanvas(CanvasWidget *c)
    {
        if (!c)
            return;
        m_canvases.removeOne(c);
        m_grid->removeWidget(c);
        relayout();
    }

    QVector<CanvasWidget *> canvases() const { return m_canvases; }
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

private:
    void relayout()
    {
        while (QLayoutItem *item = m_grid->takeAt(0))
            delete item;
        for (int i = 0; i < m_canvases.size(); ++i)
            m_grid->addWidget(m_canvases.at(i), i / m_cols, i % m_cols);
        for (int r = 0; r < 8; ++r)
            m_grid->setRowStretch(r, r < m_rows ? 1 : 0);
        for (int c = 0; c < 8; ++c)
            m_grid->setColumnStretch(c, c < m_cols ? 1 : 0);
    }

    QGridLayout *m_grid = nullptr;
    QVector<CanvasWidget *> m_canvases;
    int m_rows = 1;
    int m_cols = 1;
};

// 共享 transport 解析器：原始字节由主扩展扇出，通道名加解析器前缀避免撞名。
struct FanoutParser
{
    QString id;
    void *handle = nullptr;
    LiveSession *session = nullptr;
    QString prefix;
    PsSourceSink sink{};
};

int32_t fanoutChannel(void *ctx, const char *name, const char *unit,
                      const double *time_s, const double *value, uint32_t count)
{
    auto *ep = static_cast<FanoutParser *>(ctx);
    if (!ep || !ep->session || !name)
        return 1;
    const QByteArray full = ep->prefix.toUtf8() + '.' + QByteArray(name);
    return ep->session->sink()->channel_batch(ep->session, full.constData(), unit,
                                              time_s, value, count);
}

int32_t fanoutEvent(void *ctx, const char *topic, const char *payload)
{
    auto *ep = static_cast<FanoutParser *>(ctx);
    if (!ep || !ep->session)
        return 1;
    return ep->session->sink()->event(ep->session, topic, payload);
}

void collectPageCanvases(QWidget *root, QVector<CanvasWidget *> &out)
{
    if (!root)
        return;
    if (auto *c = dynamic_cast<CanvasWidget *>(root))
    {
        out.append(c);
        return;
    }
    if (auto *g = dynamic_cast<PlotGrid *>(root))
    {
        out += g->canvases();
        return;
    }
    // 按 splitter 子控件顺序递归，保证“第一个画布”就是屏幕左上角那块，
    // 而不是 findChildren 的先序/逆序结果。
    if (auto *sp = qobject_cast<QSplitter *>(root))
    {
        for (int i = 0; i < sp->count(); ++i)
            collectPageCanvases(sp->widget(i), out);
        return;
    }
    const auto kids = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : kids)
        collectPageCanvases(w, out);
}


QWidget *buildGridSplitter(const QVector<CanvasWidget *> &canvases, int rows, int cols, QWidget *parent)
{
    rows = std::clamp(rows, 1, 8);
    cols = std::clamp(cols, 1, 8);
    auto *root = new QSplitter(Qt::Vertical, parent);
    root->setHandleWidth(5);
    root->setChildrenCollapsible(false);
    for (int r = 0; r < rows; ++r)
    {
        auto *row = new QSplitter(Qt::Horizontal, root);
        row->setHandleWidth(5);
        row->setChildrenCollapsible(false);
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (i < canvases.size())
                row->addWidget(canvases.at(i));
        }
        root->addWidget(row);
    }
    for (int r = 0; r < rows; ++r)
        root->setStretchFactor(r, 1);
    return root;
}

} // namespace

// 字段树拖拽：Item 里存的是内核通道指针，默认 QTreeWidget 的 MIME 带不过去，自己塞一条
class ChannelTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-plotscope-channel"), QByteArray::number(ptr));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

// 源行名称按可用宽度省略中部，避免把右侧操作图标挤出去
class ElideLabel : public QLabel
{
public:
    ElideLabel(const QString &full, QWidget *parent) : QLabel(full, parent), m_full(full) {}
protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, width()));
    }
private:
    QString m_full;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);

    ps::components::HostContext hostContext;
    hostContext.window = [this] { return static_cast<QWidget *>(this); };
    hostContext.log = [this](const QString &text) { log(text); };
    hostContext.configDir = [] {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    };
    hostContext.setPlayhead = [this](double t) {
        for (CanvasWidget *c : findChildren<CanvasWidget *>())
        {
            c->setPlayhead(t);
            if (c->plotMode() == CanvasWidget::TimeSeries)
            {
                const double span = std::max(0.5, c->t1() - c->t0());
                if (t < c->t0() || t > c->t1())
                    c->setTimeRange(t - span * 0.8, t + span * 0.2);
            }
        }
    };
    m_extensions.activate(hostContext);
    for (const QString &error : m_extensions.errors())
        qWarning("[extension] %s", qPrintable(error));
    for (const ps::components::DataSourceSpec &source : m_extensions.registry().dataSources())
        DataSourceRegistry::instance().registerDescriptor(source.descriptor, {}, source.editor);

    registerCommands();

    // Ribbon 构建期就要连页签信号，先建 QTabWidget，避免 connect nullptr 警告。
    m_pages = new QTabWidget(this);

    setMenuWidget(createRibbon());
    createDocks();
    darkenDocks(this);
    createPages();
    refreshPropTree(currentCanvas());

    // 内核连通性：状态栏直接读 ABI，证明 C ABI 已经打通
    const QString kernelInfo =
        QStringLiteral("内核 ps-abi %1 · ABI v%2 · selftest %3")
            .arg(QString::fromUtf8(ps_kernel_version()))
            .arg(ps_abi_version())
            .arg(ps_abi_selftest() == 0 ? QStringLiteral("通过") : QStringLiteral("失败"));
    m_kernelInfo = kernelInfo;
    statusBar()->showMessage(kernelInfo);

    // PJ 习惯：Ctrl+S 存当前布局，Ctrl+Shift+O 打开布局
    auto *saveSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    connect(saveSc, &QShortcut::activated, this, [this] { saveLayoutTo(defaultLayoutPath()); });
    auto *openSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this);
    connect(openSc, &QShortcut::activated, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty())
            importLayoutFrom(f);
    });

    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(30);
    connect(m_liveTimer, &QTimer::timeout, this, &MainWindow::pollLiveSources);
    m_liveTimer->start();

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
}

void MainWindow::registerCommands()
{
    using Ctx = ps::commands::CommandContext;

    m_commands.add(QStringLiteral("file.open"), [this](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("打开文件"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_commands.add(QStringLiteral("file.importLayout"), [this](const Ctx &) {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty())
            importLayoutFrom(f);
    });
    m_commands.add(QStringLiteral("file.settings"), [this](const Ctx &) { openSettings(); });
    m_commands.add(QStringLiteral("file.saveLayout"), [this](const Ctx &) {
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty())
            saveLayoutTo(f);
    });
    m_commands.add(QStringLiteral("file.exportSettings"), [this](const Ctx &) {
        log(QStringLiteral("导出设置：M1 接 YAML"));
    });
    m_commands.add(QStringLiteral("file.exportData"), [this](const Ctx &) {
        log(QStringLiteral("导出数据：M1 接 CSV/NDF"));
    });
    m_commands.add(QStringLiteral("file.screenshot"), [this](const Ctx &) {
        QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
        if (!page)
            return;
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("截图另存为"), {},
            QStringLiteral("PNG (*.png)"));
        if (f.isEmpty())
            return;
        if (page->grab().save(f))
            log(QStringLiteral("已截图：%1").arg(f));
    });

    m_commands.add(QStringLiteral("data.quickOpen"), [this](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("快速打开"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_commands.add(QStringLiteral("data.recent"), [this](const Ctx &) {
        QMenu menu(this);
        QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
        const QStringList recent = st.value(QStringLiteral("recentFiles")).toStringList();
        if (recent.isEmpty())
        {
            QAction *empty = menu.addAction(QStringLiteral("暂无最近文件"));
            empty->setEnabled(false);
        }
        for (const QString &path : recent)
        {
            QAction *a = menu.addAction(QFileInfo(path).fileName());
            a->setToolTip(path);
            connect(a, &QAction::triggered, this, [this, path] { loadFile(path); });
        }
        menu.exec(QCursor::pos());
    });
    m_commands.add(QStringLiteral("data.exportSnapshot"), [this](const Ctx &) { exportActiveCsv(); });
    m_commands.add(QStringLiteral("data.reload"), [this](const Ctx &) {
        ps::data_source::SourceData *src = sourceById(m_activeSource);
        if (!src || src->path.isEmpty())
        {
            log(QStringLiteral("当前源没有可重载的文件"));
            return;
        }
        const QString path = src->path;
        QWidget *row = src->row;
        if (row)
            removeSourceRow(row);
        loadFile(path, false);
    });

    m_commands.add(QStringLiteral("page.new"), [this](const Ctx &) { newPage(); });
    m_commands.add(QStringLiteral("page.addCanvas"), [this](const Ctx &) { addCanvasToPage(); });
    m_commands.add(QStringLiteral("page.splitHorizontal"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Horizontal);
    });
    m_commands.add(QStringLiteral("page.splitVertical"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Vertical);
    });

    m_commands.add(QStringLiteral("page.grid"), [this](const Ctx &ctx) {
        const QStringList parts = ctx.arg.split(QLatin1Char(','));
        if (parts.size() == 2)
            applyCanvasGrid(parts.at(0).toInt(), parts.at(1).toInt());
    });

    m_commands.add(QStringLiteral("plot.new"), [this](const Ctx &ctx) {
        createPlotPage(ctx.arg);
    });

    m_commands.add(QStringLiteral("script.run"), [this](const Ctx &) {
        log(QStringLiteral("Lua 5.4 逐块执行（10 ms/块）：out = ps.movingAvg(ps.derivative(ps.ch(\"att.roll\")), 5)"));
    });
    m_commands.add(QStringLiteral("script.format"), [this](const Ctx &) {
        log(QStringLiteral("脚本格式化（stylua 规则，M2 接）"));
    });

    m_commands.add(QStringLiteral("view.toggleDock"), [this](const Ctx &ctx) {
        if (auto *d = findChild<QDockWidget *>(ctx.arg))
            d->setVisible(!d->isVisible());
    });
}

void MainWindow::buildRibbonWidget(SARibbonPanel *panel, const QString &id, SARibbonBar *ribbon)
{
    auto addSmall = [](SARibbonPanel *p, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        p->addSmallAction(a);
    };
    auto run = [this](const QString &cmd, const QString &arg = QString()) {
        return [this, cmd, arg] { m_commands.invoke(cmd, ps::commands::CommandContext{this, arg}); };
    };

    if (id == QLatin1String("sourceGallery"))
    {
        
        auto *srcGallery = panel->addGallery(true);
        QList<QAction *> fileActs;
        QList<QAction *> streamActs;
        auto addSource = [&](const QString &kind, const QString &label, const QString &filter,
                             QList<QAction *> &dest) {
            auto *a = new QAction(QIcon(sourceThumb(kind)), label, this);
            a->setToolTip(QStringLiteral("选择 %1 文件（可多选）").arg(label));
            connect(a, &QAction::triggered, this, [this, label, filter] {
                const QStringList fs = QFileDialog::getOpenFileNames(this, label, {}, filter);
                for (const QString &f : fs)
                    loadFile(f);
            });
            dest.append(a);
        };
        for (const DataSourceEntry &lp : DataSourceRegistry::instance().fileEntries())
        {
            const QString kind = lp.id.section(QLatin1Char('_'), -1);
            addSource(kind, lp.label, lp.filter.isEmpty() ? QStringLiteral("全部 (*)") : lp.filter, fileActs);
        }
        for (const DataSourceEntry &sp : DataSourceRegistry::instance().streamEntries())
        {
            auto *a = new QAction(QIcon(sourceThumb(sp.kind)), sp.label, this);
            a->setToolTip(QStringLiteral("连接 %1（%2）").arg(sp.label, sp.transport));
            connect(a, &QAction::triggered, this, [this, id = sp.id] { connectLiveSource(id); });
            streamActs.append(a);
        }
        auto styleGroup = [](SARibbonGalleryGroup *g) {
            if (!g)
                return;
            g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
            g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
            g->setIconSize(QSize(64, 36));
            g->setGridMinimumWidth(68);
            g->setGridMaximumWidth(74);
        };
        SARibbonGalleryGroup *firstGroup = nullptr;
        if (!fileActs.isEmpty())
        {
            firstGroup = srcGallery->addCategoryActions(QStringLiteral("日志文件"), fileActs);
            styleGroup(firstGroup);
        }
        if (!streamActs.isEmpty())
        {
            auto *g = srcGallery->addCategoryActions(QStringLiteral("实时源"), streamActs);
            styleGroup(g);
            if (!firstGroup)
                firstGroup = g;
        }
        srcGallery->setSingleRowMode(true);
        srcGallery->setMinimumWidth(280);
        srcGallery->setMaximumWidth(320);
        if (auto *more = srcGallery->findChild<QToolButton *>(QStringLiteral("SARibbonGalleryButtonMore")))
            more->hide();
        if (firstGroup)
            srcGallery->setCurrentViewGroup(firstGroup);
        panel->setMaximumWidth(380);
        if (auto *t = panel->titleLabel())
            t->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto *srcMenu = new PlotMenuPopup(this);
        if (!fileActs.isEmpty())
            srcMenu->addGroup(QStringLiteral("日志文件"), fileActs);
        if (!streamActs.isEmpty())
            srcMenu->addGroup(QStringLiteral("实时源"), streamActs);
        addSmall(panel, QStringLiteral("\u25be"), [this, ribbon, srcGallery, srcMenu] {
            srcMenu->adjustSize();
            const QPoint anchor = srcGallery->mapToGlobal(QPoint(0, 0));
            const int y = ribbon->mapToGlobal(QPoint(0, ribbon->height())).y() + 8;
            const int x = qBound(8, anchor.x(), screen()->availableGeometry().right() - srcMenu->width() - 8);
            srcMenu->move(x, y);
            srcMenu->show();
        }, this);
        return;
    }
    if (id == QLatin1String("layoutPicker"))
    {
        
        auto *layoutBtn = new QToolButton(panel);
        layoutBtn->setText(QStringLiteral("2×1  \u25be"));
        layoutBtn->setToolTip(QStringLiteral("悬停选择行×列，点击重排当前页画布"));
        layoutBtn->setPopupMode(QToolButton::InstantPopup);
        layoutBtn->setCursor(Qt::PointingHandCursor);
        layoutBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;color:#c9d1d9;font-size:11px;padding:4px 8px;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;}"));
        auto *layoutMenu = new QMenu(layoutBtn);
        auto *pick = new LayoutPickGrid(layoutMenu);
        auto *wa = new QWidgetAction(layoutMenu);
        wa->setDefaultWidget(pick);
        layoutMenu->addAction(wa);
        layoutBtn->setMenu(layoutMenu);
        pick->onPick = [this, layoutBtn, layoutMenu](int r, int c) {
            layoutBtn->setText(QStringLiteral("%1×%2  \u25be").arg(r).arg(c));
            m_commands.invoke(QStringLiteral("page.grid"),
                              ps::commands::CommandContext{this, QStringLiteral("%1,%2").arg(r).arg(c)});
            layoutMenu->close();
        };
        panel->addWidget(layoutBtn, SARibbonPanelItem::Large);
        return;
    }
    if (id == QLatin1String("viewToggles"))
    {
        
        auto *aGrid = new QAction(QStringLiteral("网格"), this);
        auto *aBox = new QAction(QStringLiteral("边框"), this);
        auto *aLegend = new QAction(QStringLiteral("图例"), this);
        auto *aRubber = new QAction(QStringLiteral("框选"), this);
        auto *aZoomIn = new QAction(QStringLiteral("放大"), this);
        auto *aZoomOut = new QAction(QStringLiteral("缩小"), this);
        auto *aAutoY = new QAction(QStringLiteral("自动 Y"), this);
        auto *aReset = new QAction(QStringLiteral("复位"), this);
        for (QAction *a : {aGrid, aBox, aLegend, aRubber})
        {
            a->setCheckable(true);
            a->setToolTip(a->text());
        }
        for (QAction *a : {aZoomIn, aZoomOut, aAutoY, aReset})
            a->setToolTip(a->text());
        const QVector<QPair<QAction *, QString>> viewActs = {
            {aGrid, QStringLiteral("grid")}, {aBox, QStringLiteral("box")},
            {aLegend, QStringLiteral("legend")}, {aRubber, QStringLiteral("ruler")},
            {aZoomIn, QStringLiteral("zoom_in")}, {aZoomOut, QStringLiteral("zoom_out")},
            {aAutoY, QStringLiteral("autoy")}, {aReset, QStringLiteral("reset")}};
        auto *viewBox = new QWidget(panel);
        auto *viewRow = new QHBoxLayout(viewBox);
        viewRow->setContentsMargins(0, 0, 0, 0);
        viewRow->setSpacing(4);
        auto makeViewGrid = [&](int first) {
            auto *box = new QWidget(viewBox);
            auto *grid = new QGridLayout(box);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setSpacing(2);
            for (int i = 0; i < 4; ++i)
            {
                QAction *a = viewActs.at(first + i).first;
                a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(viewActs.at(first + i).second)));
                auto *b = new QToolButton(box);
                b->setDefaultAction(a);
                b->setToolButtonStyle(Qt::ToolButtonIconOnly);
                b->setAutoRaise(true);
                b->setIconSize(QSize(16, 16));
                b->setStyleSheet(QStringLiteral(
                    "QToolButton{background:transparent;border:0;padding:0;}"
                    "QToolButton:hover{background:#2d3841;}"
                    "QToolButton:checked{background:#3a4a58;}"));
                b->setFixedSize(24, 24);
                grid->addWidget(b, i / 2, i % 2);
            }
            box->setFixedSize(2 * 26, 2 * 26);
            return box;
        };
        viewRow->addWidget(makeViewGrid(0));
        auto *sep = new QFrame(viewBox);
        sep->setFrameShape(QFrame::VLine);
        sep->setStyleSheet(QStringLiteral("color:#3a4650;"));
        viewRow->addWidget(sep);
        viewRow->addWidget(makeViewGrid(4));
        viewBox->setFixedSize(2 * 26 + 9 + 2 * 26, 2 * 26);
        panel->addWidget(viewBox, SARibbonPanelItem::Large);
        const QVector<CanvasWidget *> initial = pageCanvases();
        if (!initial.isEmpty())
        {
            aGrid->setChecked(initial.first()->gridVisible());
            aBox->setChecked(initial.first()->boxVisible());
            aLegend->setChecked(initial.first()->legendVisible());
            aRubber->setChecked(initial.first()->rubberZoomEnabled());
        }
        connect(aGrid, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setGridVisible(on);
        });
        connect(aBox, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setBoxVisible(on);
        });
        connect(aLegend, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setLegendVisible(on);
        });
        connect(aRubber, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setRubberZoomEnabled(on);
        });
        connect(m_pages, &QTabWidget::currentChanged, this,
                [this, aGrid, aBox, aLegend, aRubber](int) {
            const QVector<CanvasWidget *> cs = pageCanvases();
            const bool has = !cs.isEmpty();
            for (QAction *a : {aGrid, aBox, aLegend, aRubber})
                a->setEnabled(has);
            if (!has)
                return;
            aGrid->setChecked(cs.first()->gridVisible());
            aBox->setChecked(cs.first()->boxVisible());
            aLegend->setChecked(cs.first()->legendVisible());
            aRubber->setChecked(cs.first()->rubberZoomEnabled());
        });
        connect(aZoomIn, &QAction::triggered, this, [this] {
            for (CanvasWidget *c : pageCanvases()) c->zoomBy(0.8);
        });
        connect(aZoomOut, &QAction::triggered, this, [this] {
            for (CanvasWidget *c : pageCanvases()) c->zoomBy(1.25);
        });
        connect(aAutoY, &QAction::triggered, this, [this] {
            for (CanvasWidget *c : pageCanvases()) c->requestAutoY();
        });
        connect(aReset, &QAction::triggered, this, [this] {
            for (CanvasWidget *c : pageCanvases()) c->requestAutoY();
        });
        return;
    }
    if (id == QLatin1String("plotGallery"))
    {
        
        panel->setExpanding(false);
        panel->setMaximumWidth(400);
        if (auto *t = panel->titleLabel())
            t->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto *plotGallery = panel->addGallery(true);
        QList<QAction *> curveActs, mapActs, gaugeActs;
        QHash<QString, QAction *> plotActByKind;
        auto addPlot = [&](const QString &kind, const QString &label, const QString &tip,
                           QList<QAction *> &dest) {
            auto *a = new QAction(QIcon(plotThumb(kind)), label, this);
            a->setToolTip(tip);
            plotActByKind.insert(kind, a);
            connect(a, &QAction::triggered, this, run(QStringLiteral("plot.new"), kind));
            dest.append(a);
        };
        addPlot(QStringLiteral("timeseries"), QStringLiteral("时间序列"), QStringLiteral("时间轴曲线，当前已实现"), curveActs);
        addPlot(QStringLiteral("xy"), QStringLiteral("XY 曲线"), QStringLiteral("X-Y 双通道曲线"), curveActs);
        addPlot(QStringLiteral("scatter"), QStringLiteral("散点图"), QStringLiteral("二维散点"), curveActs);
        addPlot(QStringLiteral("bar"), QStringLiteral("柱状图"), QStringLiteral("离散量柱状对比"), curveActs);
        addPlot(QStringLiteral("histogram"), QStringLiteral("直方图"), QStringLiteral("样本分布"), curveActs);
        addPlot(QStringLiteral("fft"), QStringLiteral("频谱 FFT"), QStringLiteral("幅值频谱"), curveActs);
        addPlot(QStringLiteral("traj3d"), QStringLiteral("3D 轨迹"), QStringLiteral("三维空间轨迹"), mapActs);
        addPlot(QStringLiteral("map"), QStringLiteral("GPS 地图"), QStringLiteral("经纬度轨迹与网格"), mapActs);
        addPlot(QStringLiteral("surface3d"), QStringLiteral("3D 曲面"), QStringLiteral("三维曲面 / 频谱"), mapActs);
        addPlot(QStringLiteral("horizon"), QStringLiteral("地平仪"), QStringLiteral("姿态地平仪"), gaugeActs);
        addPlot(QStringLiteral("compass"), QStringLiteral("罗盘"), QStringLiteral("航向罗盘"), gaugeActs);
        addPlot(QStringLiteral("gauge"), QStringLiteral("表盘"), QStringLiteral("单值仪表盘"), gaugeActs);
        auto stylePlotGroup = [](SARibbonGalleryGroup *g) {
            g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
            g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
            g->setIconSize(QSize(64, 36));
            g->setGridMinimumWidth(70);
            g->setGridMaximumWidth(78);
        };
        auto stylePlotCommon = [](SARibbonGalleryGroup *g) {
            g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
            g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
            g->setIconSize(QSize(64, 36));
            g->setGridMinimumWidth(68);
            g->setGridMaximumWidth(74);
        };
        QList<QAction *> commonActs;
        for (const char *k : {"timeseries", "xy", "scatter", "traj3d", "map"})
            if (auto *a = plotActByKind.value(QString::fromLatin1(k)))
                commonActs.append(a);
        auto *commonGroup = plotGallery->addCategoryActions(QStringLiteral("常用"), commonActs);
        stylePlotCommon(commonGroup);
        auto *curveGroup = plotGallery->addCategoryActions(QStringLiteral("曲线"), curveActs);
        stylePlotGroup(curveGroup);
        auto *mapGroup = plotGallery->addCategoryActions(QStringLiteral("地图 / 3D"), mapActs);
        stylePlotGroup(mapGroup);
        auto *gaugeGroup = plotGallery->addCategoryActions(QStringLiteral("仪表"), gaugeActs);
        stylePlotGroup(gaugeGroup);
        plotGallery->setSingleRowMode(true);
        if (auto *more = plotGallery->findChild<QToolButton *>(QStringLiteral("SARibbonGalleryButtonMore")))
            more->hide();
        plotGallery->setMinimumWidth(330);
        plotGallery->setMaximumWidth(370);
        plotGallery->setCurrentViewGroup(commonGroup);
        if (auto *vp = plotGallery->findChild<SARibbonGalleryViewport *>())
        {
            vp->removeWidget(commonGroup);
            commonGroup->setParent(plotGallery);
        }
        auto *plotMenu = new PlotMenuPopup(this);
        plotMenu->addGroup(QStringLiteral("曲线"), curveActs);
        plotMenu->addGroup(QStringLiteral("地图 / 3D"), mapActs);
        plotMenu->addGroup(QStringLiteral("仪表"), gaugeActs);
        addSmall(panel, QStringLiteral("\u25be"), [this, ribbon, plotGallery, plotMenu] {
            plotMenu->adjustSize();
            const QPoint anchor = plotGallery->mapToGlobal(QPoint(0, 0));
            const int y = ribbon->mapToGlobal(QPoint(0, ribbon->height())).y() + 8;
            const int x = qBound(8, anchor.x(), screen()->availableGeometry().right() - plotMenu->width() - 8);
            plotMenu->move(x, y);
            plotMenu->show();
        }, this);
        return;
    }
}

QWidget *MainWindow::createRibbon()
{
    // Ribbon 结构层交给 SARibbon（MIT，vendored 在 src/3dparty/saribbon）：
    // 页签溢出、键盘导航、面板布局都是它的事；我们只负责配色与命令。
    auto *ribbon = new SARibbonBar(this);
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    // 有 Ribbon 就不要再留一条同名 QMenuBar，两排"文件/数据/绘图"并排是最刺眼的不协调
    if (auto *mb = menuBar())
        mb->hide();
    ribbon->setTitleVisible(false);       // 窗口标题在 titlebar，不占 ribbon 高度
    // 标题带直接压成 0：留 30px 空行只会把页签往下推，组标题也被挤掉
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    // 收缩：右下角 chevron + 双击页签，都能把面板收起只留页签行
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(14);
    // 分组标题：vendored 布局把标题画在面板底边内，category 高度必须把它算进去
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(78, true); // 图标 23 + 内边距 + 分组标题 14，留出基线余量

    // 大按钮同样只留图标：分组标题已说明用途，图标下再叠一行字就是原型里最挤的部分
    auto addLarge = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx); // 先用全名建 action：图标按 text 查表
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true); // Ribbon 以图标为主：文字只留 tooltip
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addLargeAction(a);
    };
    // 小按钮：只有 1~2 个字才显示文字，其余只留图标 + tooltip（图标 16px 撑得住）
    auto addSmall = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addSmallAction(a);
    };

    auto run = [this](const QString &id, const QString &arg = QString()) {
        return [this, id, arg] { m_commands.invoke(id, ps::commands::CommandContext{this, arg}); };
    };

    // 组件贡献的菜单页：一个页面可拆多个面板；页内动作只写命令 ID。
    auto renderPage = [&](const QString &title, int order = 1000) -> bool {
        bool any = false;
        SARibbonCategory *cat = nullptr;
        for (const auto &page : m_extensions.registry().ribbon())
        {
            if (page.page != title || page.order > order)
                continue;
            if (!cat)
            {
                cat = ribbon->addCategoryPage(title);
                any = true;
            }
            const QVector<ps::components::RibbonPanel> panels =
                page.panels.isEmpty() ? QVector<ps::components::RibbonPanel>{
                    ps::components::RibbonPanel{page.panel, page.items}} : page.panels;
            for (const auto &panelSpec : panels)
            {
                auto *panel = cat->addPanel(panelSpec.title);
                if (!panelSpec.widget.isEmpty())
                {
                    buildRibbonWidget(panel, panelSpec.widget, ribbon);
                    continue;
                }
                for (const auto &item : panelSpec.items)
                {
                    auto fn = [this, action = item.action, arg = item.arg] {
                        if (!m_commands.invoke(action, ps::commands::CommandContext{this, arg}))
                            m_extensions.registry().invoke(action, ps::components::ActionContext{this});
                    };
                    if (item.large)
                        addLarge(panel, item.label, fn, this);
                    else
                        addSmall(panel, item.label, fn, this);
                }
            }
        }
        return any;
    };

    // 文件
    renderPage(QStringLiteral("文件"));

    // 数据
    renderPage(QStringLiteral("数据"));

    // 绘图
    renderPage(QStringLiteral("绘图"));

    // 脚本
    renderPage(QStringLiteral("脚本"));

    // 组件贡献的协议页（其它未知页最后补）
    renderPage(QStringLiteral("协议"));

    // 视图
    renderPage(QStringLiteral("视图"));

    for (const auto &page : m_extensions.registry().ribbon())
    {
        const QStringList known = {QStringLiteral("文件"), QStringLiteral("数据"),
                                   QStringLiteral("绘图"), QStringLiteral("脚本"),
                                   QStringLiteral("协议"), QStringLiteral("视图")};
        if (!known.contains(page.page))
            renderPage(page.page);
    }

    // 右侧芯片位：只读状态摘要，跟随实际数据源刷新
    auto *rg = ribbon->rightButtonGroup();
    m_chipSource = new QAction(QStringLiteral("无数据"), this);
    m_chipLod = new QAction(QStringLiteral("LOD: Rust"), this);
    for (QAction *a : {m_chipSource, m_chipLod})
    {
        a->setEnabled(false);
        rg->addAction(a);
    }
    updateStatusChips();

    // 自绘 SVG 图标：没有图标时 SARibbon 会在按钮左侧留空方块，观感直接崩
    {
        static const QHash<QString, QString> kIcon = {
            {QStringLiteral("打开文件"), QStringLiteral("open")},
            {QStringLiteral("导入布局"), QStringLiteral("import")},
            {QStringLiteral("设置"), QStringLiteral("settings")},
            {QStringLiteral("保存布局"), QStringLiteral("export")},
            {QStringLiteral("导出布局"), QStringLiteral("export")},
            {QStringLiteral("导出设置"), QStringLiteral("export_cfg")},
            {QStringLiteral("导出数据"), QStringLiteral("export_data")},
            {QStringLiteral("截图当前页"), QStringLiteral("screenshot")},
            {QStringLiteral("打开数据源"), QStringLiteral("datasource")},
            {QStringLiteral("全部开始"), QStringLiteral("play")},
            {QStringLiteral("全部暂停"), QStringLiteral("pause")},
            {QStringLiteral("新建页"), QStringLiteral("newpage")},
            {QStringLiteral("添加画布"), QStringLiteral("plus")},
            {QStringLiteral("自动 Y"), QStringLiteral("autoy")},
            {QStringLiteral("游标"), QStringLiteral("cursor")},
            {QStringLiteral("复位视图"), QStringLiteral("reset")},
            {QStringLiteral("地平仪"), QStringLiteral("horizon")},
            {QStringLiteral("罗盘"), QStringLiteral("compass")},
            {QStringLiteral("表盘"), QStringLiteral("gauge")},
            {QStringLiteral("运行"), QStringLiteral("run")},
            {QStringLiteral("格式化"), QStringLiteral("format")},
            {QStringLiteral("新建协议"), QStringLiteral("newpage")},
            {QStringLiteral("打开编辑"), QStringLiteral("open")},
            {QStringLiteral("校验协议"), QStringLiteral("validate")},
            {QStringLiteral("新建 Loader"), QStringLiteral("loader")},
            {QStringLiteral("帧模板 YAML"), QStringLiteral("yaml")},
            {QStringLiteral("校验"), QStringLiteral("validate")},
            {QStringLiteral("数据源"), QStringLiteral("datasource")},
            {QStringLiteral("属性"), QStringLiteral("prop")},
            {QStringLiteral("脚本"), QStringLiteral("script")},
            {QStringLiteral("日志"), QStringLiteral("log")},
            {QStringLiteral("视频"), QStringLiteral("play")},
            {QStringLiteral("网格"), QStringLiteral("grid")},
            {QStringLiteral("边框"), QStringLiteral("box")},
            {QStringLiteral("图例"), QStringLiteral("legend")},
            {QStringLiteral("框选"), QStringLiteral("cursor")},
            {QStringLiteral("放大"), QStringLiteral("zoom_in")},
            {QStringLiteral("缩小"), QStringLiteral("zoom_out")},
            {QStringLiteral("向右拆分"), QStringLiteral("split_h")},
            {QStringLiteral("向下拆分"), QStringLiteral("split_v")},
            {QStringLiteral("最近"), QStringLiteral("import")},
            {QStringLiteral("导出快照"), QStringLiteral("export_data")},
            {QStringLiteral("重载"), QStringLiteral("reset")},
            {QStringLiteral("复位"), QStringLiteral("reset")},
            {QStringLiteral("\u25be"), QStringLiteral("arrow_more")},
        };
        // QAction 的 parent 是 this（不是 ribbon），按 ribbon 找会一个都拿不到
        for (auto *a : findChildren<QAction *>())
        {
            const auto it = kIcon.find(a->text());
            if (it != kIcon.end())
            {
                a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(it.value())));
                if (a->property("rbIconOnly").toBool())
                    a->setText(QString());

            }
        }
    }

    if (auto *ab = ribbon->applicationButton())
        ab->hide(); // 左上角默认有个空按钮，留白很难看

    // 悬停提示改状态栏：QToolTip 会飘在按钮下方，正好盖住分组标题
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setToolTip(QString());
        if (auto *a = b->defaultAction())
        {
            b->setProperty("rbTip", a->toolTip());
            a->setToolTip(QString());
        }
        b->installEventFilter(this);
    }

    // 面板本体撑到 68 px：SARibbon 的 category 高度不会自动把 panel 拉满
    for (auto *p : ribbon->findChildren<SARibbonPanel *>())
    {
        p->setMinimumHeight(56);
        if (auto *l = p->layout())
            l->setContentsMargins(4, 4, 9, 4);
    }

    // 大按钮 54×54、小按钮单行 22：只用 QSS 分不开两种形态，先打属性标记
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setProperty("rbLarge", b->buttonType() == SARibbonToolButton::LargeButton);
    }

    // 静态库里资源要显式初始化，否则 dark 主题静默回退浅色 QSS
    Q_INIT_RESOURCE(SARibbonResource);
    SA::applyRibbonTheme(this, ribbon, SARibbonTheme::RibbonThemeDark);
    // 视觉对齐 design/prototype/PlotScope-工作区原型.html 的 ribbon 段：
    // 扁平、无边框、琥珀色选中，压掉 Office 味的蓝色和内边距
    ribbon->setStyleSheet(ribbon->styleSheet() + QStringLiteral(R"(
        SARibbonBar { background:#141a1f; border-bottom:1px solid #232b33; }
        SARibbonCategory { background:#141a1f; border:0; }
        SARibbonPanel { background:#141a1f; border:0; border-right:1px solid #1b232a; }
        SARibbonPanelLabel { color:#7d8b96; font-size:10px; background:transparent; padding-bottom:3px; }
        SARibbonToolButton {
            background:transparent; border:1px solid transparent; border-radius:0;
            color:#8494a0; font-size:12px; padding:0; margin:0;
        }
        SARibbonToolButton[rbLarge="true"] { min-width:52px; max-width:60px; min-height:48px; max-height:48px; }
        SARibbonToolButton[rbLarge="false"] { min-height:21px; max-height:22px; padding:2px 8px; }
        SARibbonToolButton:hover { background:#1b232a; color:#c8d2da; border-color:#222b33; }
        SARibbonToolButton:pressed { background:#212a31; }
        SARibbonToolButton:checked { background:#212a31; color:#f2b134; border-color:#222b33; }
        SARibbonToolButton:disabled { color:#4a545d; }
        SARibbonToolButton::menu-indicator { image:none; width:0; }
        SARibbonSeparatorWidget { background:#1b232a; }
        SARibbonPanelOptionButton { background:transparent; border:0; }
        SARibbonPanelOptionButton:hover { background:#1b232a; }
        #objSARibbonTabBar { background:#141a1f; border:0; }
        #objSARibbonTabBar::tab {
            background:transparent; color:#5d6c77; font-size:11px;
            padding:0 11px; margin:0 1px; border:0; border-bottom:2px solid transparent;
            min-width:0;
        }
        #objSARibbonTabBar::tab:hover { color:#c8d2da; background:#1a2127; }
        #objSARibbonTabBar::tab:selected { color:#f2b134; border-bottom:2px solid #f2b134; }
        #objSARibbonQuickAccessBar { background:#141a1f; }
    )"));
    return ribbon;
}
QDockWidget *MainWindow::buildDockWidget(const QString &id, QWidget *parent, const QString &icon)
{
    if (id == QLatin1String("dock.src"))
    {
    // 左：数据源（对齐原型 d-src）：源列表 → 字段/参数/消息 三页签 → 内容
    auto *srcDock = new QDockWidget(QStringLiteral("数据源"), this);
    srcDock->setObjectName(QStringLiteral("dock.src"));
    if (!icon.isEmpty())
        srcDock->setProperty("psIcon", icon);

    // 标题栏：标题 + 汇总 + 全局起停 + 关闭，全部放一行，正文从源列表直接开始（不再叠第二行头部）
    auto *srcBar = new DockTitleBar(this, srcDock);
    srcBar->setStyleSheet(QStringLiteral(
        "background:#26303a;border-bottom:1px solid #3a4854;"));
    auto *barLay = new QHBoxLayout(srcBar);
    barLay->setContentsMargins(10, 0, 6, 0);
    barLay->setSpacing(6);
    auto *barIcon = new QLabel(srcBar);
    barIcon->setPixmap(QIcon(dockIconResource(srcDock)).pixmap(14, 14));
    barIcon->setStyleSheet(QStringLiteral("background:transparent;"));
    barIcon->setFixedSize(16, 16);
    barLay->addWidget(barIcon);
    auto *barTitle = new QLabel(QStringLiteral("数据源"), srcBar);
    barTitle->setStyleSheet(QStringLiteral(
        "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
    barLay->addWidget(barTitle);
    m_srcCount = new QLabel(QStringLiteral("0 源 · 0 已连接"), srcBar);
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
    barLay->addWidget(m_srcCount);
    barLay->addStretch();
    auto mkTiny = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(srcBar);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
        b->setIconSize(QSize(14, 14));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(22, 22);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#8494a0;padding:0;}"
            "QToolButton:hover{background:#222b33;}"));
        barLay->addWidget(b);
        return b;
    };
    auto *addSrcBtn = mkTiny(QStringLiteral("newpage"), QStringLiteral("添加数据源"));
    QObject::connect(addSrcBtn, &QToolButton::clicked, this, [this] {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("添加数据源"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    auto *dockMenuBtn = new QToolButton(srcBar);
    dockMenuBtn->setText(QStringLiteral("▾"));
    dockMenuBtn->setToolTip(QStringLiteral("最大化 / 最小化 / 浮动 / 关闭"));
    dockMenuBtn->setCursor(Qt::PointingHandCursor);
    dockMenuBtn->setFixedSize(30, 26);
    dockMenuBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
        "padding:0 0 2px 0;font-size:15px;font-weight:600;}"
        "QToolButton:hover{background:#222b33;border-color:#f2b134;color:#f2b134;}"));
    auto *dockMenu = new QMenu(dockMenuBtn);
    QAction *maxDock = dockMenu->addAction(QStringLiteral("最大化 / 还原"));
    QAction *toLeft = dockMenu->addAction(QStringLiteral("停靠到左侧"));
    QAction *toRight = dockMenu->addAction(QStringLiteral("停靠到右侧"));
    QAction *toBottom = dockMenu->addAction(QStringLiteral("停靠到底部"));
    dockMenu->addSeparator();
    QAction *collapse = dockMenu->addAction(QStringLiteral("最小化（自动隐藏）"));
    QAction *floatAct = dockMenu->addAction(QStringLiteral("浮动 / 取消浮动"));
    dockMenu->addSeparator();
    QAction *closeAct = dockMenu->addAction(QStringLiteral("关闭面板"));
    QObject::connect(maxDock, &QAction::triggered, this, [this, srcDock] { toggleDockMax(srcDock); });
    QObject::connect(toLeft, &QAction::triggered, this, [this, srcDock] { restoreDockMax(); dockToArea(srcDock, this, Qt::LeftDockWidgetArea); });
    QObject::connect(toRight, &QAction::triggered, this, [this, srcDock] { dockToArea(srcDock, this, Qt::RightDockWidgetArea); });
    QObject::connect(toBottom, &QAction::triggered, this, [this, srcDock] { dockToArea(srcDock, this, Qt::BottomDockWidgetArea); });
    QObject::connect(collapse, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        collapseDock(srcDock, this);
    });
    QObject::connect(floatAct, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        srcDock->setFloating(!srcDock->isFloating());
    });
    QObject::connect(closeAct, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        srcDock->close();
    });
    dockMenuBtn->setPopupMode(QToolButton::InstantPopup);
    QObject::connect(dockMenuBtn, &QToolButton::clicked, dockMenuBtn, [dockMenu, dockMenuBtn] {
        execDockMenu(dockMenuBtn, dockMenu);
    });
    barLay->addWidget(dockMenuBtn);
    srcDock->setTitleBarWidget(srcBar);

    auto *panel = new ps::data_source::DataSourceDock(
        ps::data_source::DataSourceDock::Context{
            [this](const QStringList &files) {
                for (const QString &f : files)
                    loadFile(f);
            },
            [this](const QString &id) { connectLiveSource(id); },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                    c->addChannel(ch);
            },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                {
                    c->setChannels({ch});
                    c->setTitle(QString::fromUtf8(ch->name));
                }
            },
            [this](int id) { return sourceById(id); },
            [this] { return m_activeSource; },
            [this] { refreshActiveSource(); },
            [this](const QString &name) { addTransformFor(name); },
            [this](double t, const QString &topic, bool prompt) {
                addTimeMarker(t, topic, prompt);
            },
            [this](const QString &text) { log(text); },
            [this] { return !m_sourceStore.items().isEmpty(); },
            [this]() -> QVector<SourceData> & { return m_sourceStore.items(); },
            [this](const QString &name) {
                for (auto *c : findChildren<CanvasWidget *>())
                    c->removeChannel(name);
            },
            [this] { return m_srcMsgs; },
            [this](SourceData *src, bool paused) { setSourcePaused(src, paused); },
            [this](SourceData *src) { openSourceDebug(src); },
            [this](SourceData *src) { configureLiveSource(src); },
            [this](QWidget *row) { removeSourceRow(row); },
            [this](QWidget *row) { activateSource(row); },
            [this](QWidget *row) { row->installEventFilter(this); },
            [this](int id, QWidget *row) {
                for (SourceData &src : m_sourceStore.items())
                    if (src.id == id)
                        src.row = row;
            },
            [this](const QString &path) { loadFile(path); },
            [this](SourceData *src, const QString &name) {
                src->alias = name;
                for (auto *c : findChildren<CanvasWidget *>())
                    c->renameSource(src->path, name);
            },
            [this](SourceData *src, int mode) { alignSource(src, mode); },
            [this](SourceData *src) { setSourceBase(src); },
            [this](SourceData *src, double off, bool refit) { setSourceOffset(src, off, refit); },
            [this](SourceData *src, SourceData *other, bool all, const QString &pick) {
                compareSources(src, other, all, pick);
            },
            [this](SourceData *src) { closeLiveSource(src); },
            [this](const QString &path) {
                for (auto *c : findChildren<CanvasWidget *>())
                    c->removeChannelsFromSource(path);
            },
            [this](int id) { m_activeSource = id; },
            [this] {
                m_viewBacked = false;
                m_srcChannels.clear();
                m_srcParams.clear();
                m_srcMsgs.clear();
            },
            [this] { renderSourceData(); },
            [this] { refreshPropTree(currentCanvas()); },
            [this](SourceData &src, bool first) {
                if (first)
                {
                    QVector<const PsChannelInfo *> pick;
                    for (const PsChannelInfo *ch : src.channels)
                    {
                        if (!ch || ch->point_count < 8) continue;
                        double lo = ch->value[0], hi = lo;
                        for (uint32_t k = 1; k < ch->point_count; ++k)
                        {
                            lo = qMin(lo, ch->value[k]);
                            hi = qMax(hi, ch->value[k]);
                        }
                        if (hi - lo > 1e-9) pick.append(ch);
                    }
                    if (pick.isEmpty()) pick = src.channels;
                    const QVector<CanvasWidget *> canvases = pageCanvases();
                    for (int i = 0; i < canvases.size() && i < pick.size(); ++i)
                    {
                        canvases[i]->setInputSource(src.id, src.path, src.alias, src.offsetSec);
                        canvases[i]->setChannels({pick[i]});
                        canvases[i]->setTitle(QStringLiteral("%1 · %2")
                                              .arg(src.alias, QString::fromUtf8(pick[i]->name)));
                    }
                }
            },
            [this](const QString &path) {
                QSettings recentStore(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
                QStringList recent = recentStore.value(QStringLiteral("recentFiles")).toStringList();
                recent.removeAll(path);
                recent.prepend(path);
                while (recent.size() > 12) recent.removeLast();
                recentStore.setValue(QStringLiteral("recentFiles"), recent);
            },
            [this](const QString &msg) { statusBar()->showMessage(msg); },
            [this](SourceData *src) {
                if (!src || src->id != m_activeSource) return;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_liveUiMs < 200 && !m_srcChannels.isEmpty()) return;
                m_liveUiMs = now;
                m_srcChannels = src->channels;
                renderSourceData();
                if (CanvasWidget *c = currentCanvas())
                {
                    c->followLatest();
                    c->notifyActivated();
                }
                for (auto *c : findChildren<CanvasWidget *>()) c->update();
                if (src->plotted || !currentCanvas()) return;
                QVector<const PsChannelInfo *> first;
                auto continuous = [](const PsChannelInfo *ch) {
                    if (!ch || ch->point_count < 2) return false;
                    const QString n = QString::fromUtf8(ch->name ? ch->name : "");
                    for (const QString &skip : {QStringLiteral("status"), QStringLiteral("type"),
                                                QStringLiteral("datum"), QStringLiteral("num_"),
                                                QStringLiteral("id"), QStringLiteral("flag")})
                        if (n.contains(skip, Qt::CaseInsensitive)) return false;
                    return true;
                };
                for (const PsChannelInfo *ch : src->channels)
                {
                    if (continuous(ch)) first.append(ch);
                    if (first.size() >= 2) break;
                }
                if (first.size() < 2)
                {
                    first.clear();
                    for (const PsChannelInfo *ch : src->channels)
                    {
                        if (ch && ch->point_count >= 2) first.append(ch);
                        if (first.size() >= 2) break;
                    }
                }
                if (first.isEmpty()) return;
                if (currentCanvas()->hasData()) newPage();
                if (CanvasWidget *c = currentCanvas())
                    c->setInputSource(src->id, src->path, src->alias, src->offsetSec);
                for (const PsChannelInfo *ch : first) currentCanvas()->addChannel(ch);
                currentCanvas()->setTitle(src->alias);
                currentCanvas()->setFollowLive(true, 30.0);
                src->plotted = true;
            },
            [this](SourceData *src) {
                m_activeSource = src->id;
                m_viewBacked = true;
                m_srcFmt = src->fmt;
                m_srcChannels = src->channels;
                m_srcParams = src->params;
                m_srcMsgs = src->msgs;
                for (auto *c : findChildren<CanvasWidget *>())
                    c->setInputSource(src->id, src->path, src->alias, src->offsetSec);
                renderSourceData();
            },
            [this](QWidget *row) { showSourceProps(row); },
            [this] { updateStatusChips(); },

        },
        this);
    m_dataSourceDock = panel;
    panel->m_srcCount = m_srcCount;
    m_srcCount = panel->m_srcCount;
    m_srcStatus = panel->m_srcStatus;
    m_srcRows = panel->m_srcRows;
    m_srcListToggle = panel->m_srcListToggle;
    m_srcListScroll = panel->m_srcListScroll;
    m_srcTabs = panel->m_srcTabs;
    m_srcFilter = panel->m_srcFilter;
    m_srcStack = panel->m_srcStack;
    m_srcTree = panel->m_srcTree;
    m_paramTable = panel->m_paramTable;
    m_msgTable = panel->m_msgTable;
    m_srcEmpty = panel->m_srcEmpty;
    m_srcChips = panel->m_srcChips;
    srcDock->setWidget(panel);
    return srcDock;
    return srcDock;
    }
    if (id == QLatin1String("dock.prop"))
    {
    // 右：属性面板（命中什么对象就显示什么属性）
    auto *propDock = new QDockWidget(QStringLiteral("属性面板"), this);
    propDock->setObjectName(QStringLiteral("dock.prop"));
    if (!icon.isEmpty())
        propDock->setProperty("psIcon", icon);
    propDock->setMinimumHeight(340);
    auto *propHost = new QWidget(propDock);
    auto *propLay = new QVBoxLayout(propHost);
    propLay->setContentsMargins(0, 0, 0, 0);
    auto *propHead = new QWidget(propHost);
    auto *propHeadLay = new QHBoxLayout(propHead);
    propHeadLay->setContentsMargins(8, 4, 6, 4);
    propHeadLay->setSpacing(6);
    m_selLabel = new QLabel(QStringLiteral("当前对象：画布"), propHead);
    m_selLabel->setStyleSheet(QStringLiteral("color:#4ea1ff;"));
    m_propEditBtn = new QToolButton(propHead);
    m_propEditBtn->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    m_propEditBtn->setIconSize(QSize(14, 14));
    m_propEditBtn->setText(QStringLiteral("编辑"));
    m_propEditBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_propEditBtn->setCursor(Qt::PointingHandCursor);
    m_propEditBtn->setToolTip(QStringLiteral("编辑当前曲线属性"));
    m_propEditBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
        "padding:1px 6px;}QToolButton:hover{border-color:#f2b134;color:#f2b134;}"));
    m_propEditBtn->setEnabled(false);
    propHeadLay->addWidget(m_selLabel, 1);
    propHeadLay->addWidget(m_propEditBtn);
    m_propTable = new QTableWidget(0, 2, propHost);
    m_propTable->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_propTable->horizontalHeader()->setStretchLastSection(true);
    m_propTable->verticalHeader()->setVisible(false);
    m_propTable->setShowGrid(false);
    compactValueTable(m_propTable);
    connect(m_propEditBtn, &QToolButton::clicked, this, [this] {
        if (!m_propCanvas)
            return;
        if (m_propKind == 2 && m_propSeriesIndex >= 0)
            editSeriesProps(m_propCanvas, m_propSeriesIndex);
        else if (m_propKind == 1)
            editAxesProps(m_propCanvas);
    });
    connect(m_propTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (!m_propCanvas)
            return;
        if (m_propKind == 2 && m_propSeriesIndex >= 0)
            editSeriesProps(m_propCanvas, m_propSeriesIndex);
        else if (m_propKind == 1)
            editAxesProps(m_propCanvas);
    });
    propLay->addWidget(propHead);
    propLay->addWidget(m_propTable, 1);
    propDock->setWidget(propHost);
    setPropRows(QStringLiteral("画布"),
                {{QStringLiteral("提示"), QStringLiteral("选中绘图页签后显示属性")},
                 {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩放 / 拖动平移 / 双击复位")}});
    return propDock;
    }
    if (id == QLatin1String("dock.tree"))
    {
    // 顶右：对象树独立 Dock（属性面板只保留属性表）
    auto *treeDock = new QDockWidget(QStringLiteral("对象树"), this);
    treeDock->setObjectName(QStringLiteral("dock.tree"));
    if (!icon.isEmpty())
        treeDock->setProperty("psIcon", icon);
    treeDock->setMinimumHeight(180);
    auto *treeHost = new QWidget(treeDock);
    auto *treeLay = new QVBoxLayout(treeHost);
    treeLay->setContentsMargins(0, 0, 0, 0);
    m_propTree = new QTreeWidget(treeHost);
    m_propTree->setHeaderHidden(true);
    m_propTree->setColumnCount(1);
    m_propTree->setRootIsDecorated(true);
    m_propTree->setIndentation(13);
    m_propTree->setUniformRowHeights(true);
    m_propTree->setMinimumHeight(150);
    m_propTree->setStyleSheet(QStringLiteral(
        "QTreeWidget{border:0;background:#0d1117;font-size:12px;}"
        "QTreeWidget::item{height:20px;color:#c9d1d9;}"
        "QTreeWidget::item:selected{background:#1f2a33;color:#f2b134;}"));
    treeLay->addWidget(m_propTree);
    treeDock->setWidget(treeHost);
    connect(m_propTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *it, QTreeWidgetItem *) {
        if (m_propTreeBuilding || !it)
            return;
        const int kind = it->data(0, Qt::UserRole).toInt();
        CanvasWidget *c = reinterpret_cast<CanvasWidget *>(
            it->data(0, Qt::UserRole + 1).value<quintptr>());
        if (!c)
            return;
        if (kind == 1)
            showAxesProps(c);
        else if (kind == 2)
            showSeriesProps(c, it->data(0, Qt::UserRole + 2).toInt());
    });
    // 同一行重复点击时 currentItemChanged 不会再发；补 itemClicked 保证属性始终跟随。
    connect(m_propTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *it, int) {
        if (m_propTreeBuilding || !it)
            return;
        const int kind = it->data(0, Qt::UserRole).toInt();
        CanvasWidget *c = reinterpret_cast<CanvasWidget *>(
            it->data(0, Qt::UserRole + 1).value<quintptr>());
        if (!c)
            return;
        if (kind == 1)
            showAxesProps(c);
        else if (kind == 2)
            showSeriesProps(c, it->data(0, Qt::UserRole + 2).toInt());
    });
    return treeDock;
    }
    if (id == QLatin1String("dock.script"))
    {
    // 右下：脚本
    auto *scriptDock = new QDockWidget(QStringLiteral("脚本 · Lua 5.4"), this);
    scriptDock->setObjectName(QStringLiteral("dock.script"));
    if (!icon.isEmpty())
        scriptDock->setProperty("psIcon", icon);
    auto *scriptHost = new QWidget(scriptDock);
    auto *scriptLay = new QVBoxLayout(scriptHost);
    scriptLay->setContentsMargins(0, 0, 0, 0);
    m_script = new QPlainTextEdit(scriptHost);
    QFont scriptFont(QStringLiteral("monospace"));
    scriptFont.setStyleHint(QFont::Monospace);
    scriptFont.setPixelSize(12);
    m_script->setFont(scriptFont);
    m_script->setTabStopDistance(28);
    m_script->setPlainText(QStringLiteral(
        "-- 绑定面板可选 1–4 个输入，v1..v4 或 ps.input(n)\n"
        "rate = ps.derivative(ps.input(1), ps.time())\n"
        "out = ps.movingAvg(rate, 5)\n"
        "ps.plot(\"roll_rate\", out)\n"));
    auto *bindRow = new QHBoxLayout;
    bindRow->addWidget(new QLabel(QStringLiteral("绑定"), scriptHost));
    for (int i = 0; i < 4; ++i)
    {
        auto *cb = new QComboBox(scriptHost);
        cb->addItem(QStringLiteral("（未绑定）"), QString());
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cb->setMinimumContentsLength(8);
        bindRow->addWidget(cb, 1);
        m_scriptInputs[i] = cb;
    }
    scriptLay->addLayout(bindRow);

    // init(params)：两列表单，名称 + 数值。
    auto *paramHead = new QHBoxLayout;
    paramHead->addWidget(new QLabel(QStringLiteral("参数"), scriptHost));
    auto *addParam = new QToolButton(scriptHost);
    addParam->setText(QStringLiteral("+"));
    addParam->setToolTip(QStringLiteral("添加参数"));
    auto *delParam = new QToolButton(scriptHost);
    delParam->setText(QStringLiteral("-"));
    delParam->setToolTip(QStringLiteral("删除选中参数"));
    paramHead->addStretch(1);
    paramHead->addWidget(addParam);
    paramHead->addWidget(delParam);
    scriptLay->addLayout(paramHead);
    m_scriptParams = new QTableWidget(0, 2, scriptHost);
    m_scriptParams->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("值")});
    m_scriptParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_scriptParams->verticalHeader()->setVisible(false);
    m_scriptParams->setMaximumHeight(110);
    scriptLay->addWidget(m_scriptParams);
    connect(addParam, &QToolButton::clicked, this, [this] {
        const int row = m_scriptParams->rowCount();
        m_scriptParams->insertRow(row);
        m_scriptParams->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0")));
        m_scriptParams->editItem(m_scriptParams->item(row, 0));
    });
    connect(delParam, &QToolButton::clicked, this, [this] {
        const int row = m_scriptParams->currentRow();
        if (row >= 0)
            m_scriptParams->removeRow(row);
    });

    auto *runBtnRow = new QHBoxLayout;
    auto *runScriptBtn = new QPushButton(QStringLiteral("预览"), scriptHost);
    auto *applyScriptBtn = new QPushButton(QStringLiteral("应用"), scriptHost);
    runBtnRow->addWidget(runScriptBtn);
    runBtnRow->addWidget(applyScriptBtn);
    scriptLay->addWidget(m_script, 1);
    scriptLay->addLayout(runBtnRow);
    scriptDock->setWidget(scriptHost);
    connect(runScriptBtn, &QPushButton::clicked, this, [this] { runScript(false); });
    connect(applyScriptBtn, &QPushButton::clicked, this, [this] { runScript(true); });
    return scriptDock;
    }
    if (id == QLatin1String("dock.log"))
    {
    // 底部：日志
    auto *logDock = new QDockWidget(QStringLiteral("日志"), this);
    logDock->setObjectName(QStringLiteral("dock.log"));
    if (!icon.isEmpty())
        logDock->setProperty("psIcon", icon);
    m_logView = new QPlainTextEdit(logDock);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    logDock->setWidget(m_logView);
    return logDock;
    }
    Q_UNUSED(parent);
    return nullptr;
}

void MainWindow::createDocks()
{
    auto docks = m_extensions.registry().docks();
    std::sort(docks.begin(), docks.end(),
              [](const ps::components::DockContribution &a, const ps::components::DockContribution &b) {
                  return a.order < b.order;
              });
    for (const auto &spec : docks)
    {
        QWidget *created = spec.create ? spec.create(this) : nullptr;
        QDockWidget *dock = qobject_cast<QDockWidget *>(created);
        if (!dock && created)
        {
            dock = new QDockWidget(spec.title, this);
            dock->setWidget(created);
        }
        if (!dock)
            dock = buildDockWidget(spec.id, this, spec.icon);
        if (!dock)
            continue;
        if (!spec.id.isEmpty())
            dock->setObjectName(spec.id);
        if (!spec.title.isEmpty())
            dock->setWindowTitle(spec.title);
        addDockWidget(spec.area, dock);
        dock->setVisible(spec.visible);
    }

    QDockWidget *propDock = findChild<QDockWidget *>(QStringLiteral("dock.prop"));
    QDockWidget *treeDock = findChild<QDockWidget *>(QStringLiteral("dock.tree"));
    QDockWidget *scriptDock = findChild<QDockWidget *>(QStringLiteral("dock.script"));
    QDockWidget *logDock = findChild<QDockWidget *>(QStringLiteral("dock.log"));
    if (propDock && treeDock)
        splitDockWidget(propDock, treeDock, Qt::Vertical);
    if (propDock && scriptDock)
        resizeDocks({propDock, scriptDock}, {470, 330}, Qt::Vertical);
    if (logDock)
        resizeDocks({logDock}, {96}, Qt::Vertical);
    refreshSrcCount();
}

void MainWindow::toggleDockMax(QDockWidget *dock)
{
    if (!dock)
        return;
    if (m_maxDock == dock)
    {
        restoreDockMax();
        return;
    }
    restoreDockMax();
    if (dock->isFloating())
    {
        const Qt::DockWidgetArea area = dockWidgetArea(dock);
        dock->setFloating(false);
        addDockWidget(area, dock);
    }
    m_maxDockCentral = centralWidget();
    if (m_maxDockCentral)
        m_maxDockCentral->hide();
    m_maxDockHidden.clear();
    for (QDockWidget *o : findChildren<QDockWidget *>())
        if (o != dock && o->isVisible())
        {
            m_maxDockHidden.append(o);
            o->hide();
        }
    dock->show();
    dock->raise();
    m_maxDock = dock;
    log(QStringLiteral("最大化面板：%1").arg(dock->windowTitle()));
}

void MainWindow::restoreDockMax()
{
    if (!m_maxDock)
        return;
    for (QDockWidget *o : m_maxDockHidden)
        if (o)
            o->show();
    m_maxDockHidden.clear();
    if (m_maxDockCentral)
        m_maxDockCentral->show();
    m_maxDock = nullptr;
}

static QString dockIconResource(const QDockWidget *d)
{
    const QString icon = d->property("psIcon").toString();
    return icon.isEmpty() ? QStringLiteral(":/icons/panel.svg") : icon;
}

// 出现在窗口最右缘的 ▾ 菜单必须左对齐弹出，否则菜单会跑到屏幕外。
static void execDockMenu(QToolButton *btn, QMenu *menu)
{
    const QPoint below = btn->mapToGlobal(QPoint(btn->width(), btn->height()));
    const QPoint topLeft = btn->mapToGlobal(QPoint(0, 0));
    const int w = menu->sizeHint().width();
    const int h = menu->sizeHint().height();
    QRect avail = btn->screen() ? btn->screen()->availableGeometry()
                                : QRect(0, 0, 1920, 1080);
    QPoint pos(below.x() - w, below.y());
    pos.setX(qMax(avail.left() + 4, qMin(pos.x(), avail.right() - w - 4)));
    if (pos.y() + h > avail.bottom())
        pos.setY(topLeft.y() - h);
    menu->exec(pos);
}

// dock 标题栏在部分平台/主题下会画成浅灰，QSS 与 palette 都压不住：
// 直接换成自绘的深色标题条（26px，和原型 dock-h 一致），顺带把关闭按钮收进来
void darkenDocks(MainWindow *w)
{
    for (auto *d : w->findChildren<QDockWidget *>())
    {
        if (d->titleBarWidget())
            continue; // 数据源 dock 自带一行式标题栏，别盖一层

        auto *bar = new DockTitleBar(w, d);
        bar->setStyleSheet(QStringLiteral(
            "background:#26303a;border-bottom:1px solid #3a4854;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(4);

        auto *icon = new QLabel(bar);
        icon->setPixmap(QIcon(dockIconResource(d)).pixmap(14, 14));
        icon->setStyleSheet(QStringLiteral("background:transparent;"));
        icon->setFixedSize(16, 16);
        lay->addWidget(icon);

        auto *title = new QLabel(d->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral(
            "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        // 所有停靠/浮靠/关闭集中到一个菜单，避免标题栏塞满小按钮
        auto *menuBtn = new QToolButton(bar);
        menuBtn->setText(QStringLiteral("▾"));
        menuBtn->setToolTip(QStringLiteral("最大化 / 最小化 / 浮动 / 关闭"));
        menuBtn->setCursor(Qt::PointingHandCursor);
        menuBtn->setFixedSize(32, 26);
        menuBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
            "padding:0;font-size:17px;font-weight:700;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;background:#222b33;}"));
        auto *menu = new QMenu(menuBtn);
        QAction *maximize = menu->addAction(QStringLiteral("最大化 / 还原"));
        QAction *left = menu->addAction(QStringLiteral("停靠到左侧"));
        QAction *right = menu->addAction(QStringLiteral("停靠到右侧"));
        QAction *bottom = menu->addAction(QStringLiteral("停靠到底部"));
        menu->addSeparator();
        QAction *autoHide = menu->addAction(QStringLiteral("最小化（自动隐藏）"));
        QAction *floatAct = menu->addAction(QStringLiteral("浮动 / 取消浮动"));
        menu->addSeparator();
        QAction *close = menu->addAction(QStringLiteral("关闭面板"));
        QObject::connect(left, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::LeftDockWidgetArea); });
        QObject::connect(right, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::RightDockWidgetArea); });
        QObject::connect(bottom, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::BottomDockWidgetArea); });
        QObject::connect(maximize, &QAction::triggered, w, [w, d] { w->toggleDockMax(d); });
        QObject::connect(autoHide, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            collapseDock(d, w);
        });
        QObject::connect(floatAct, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            d->setFloating(!d->isFloating());
        });
        QObject::connect(close, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            d->close();
        });
        menuBtn->setMenu(menu);
        QObject::connect(menuBtn, &QToolButton::clicked, menuBtn, [menuBtn, menu] {
            execDockMenu(menuBtn, menu);
        });
        lay->addWidget(menuBtn);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bar, &QWidget::customContextMenuRequested, bar, [bar, menu](const QPoint &pos) {
            menu->exec(bar->mapToGlobal(pos));
        });

        d->setTitleBarWidget(bar);
    }
}

void MainWindow::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    m_dataSourceDock->showSourceMenu(row, globalPos);
}

void MainWindow::removeSourceRow(QWidget *row)
{
    m_dataSourceDock->removeSourceRow(row);
}

void MainWindow::updateStatusChips()
{
    if (!m_chipSource)
        return;
    int channels = 0;
    for (const ps::data_source::SourceData &s : m_sourceStore.items())
        channels += s.channels.size();
    if (m_sourceStore.items().isEmpty())
        m_chipSource->setText(QStringLiteral("无数据"));
    else if (std::any_of(m_sourceStore.items().cbegin(), m_sourceStore.items().cend(),
                         [](const ps::data_source::SourceData &s) { return s.live && s.paused; }))
    {
        const int paused = int(std::count_if(m_sourceStore.items().cbegin(), m_sourceStore.items().cend(),
                                             [](const ps::data_source::SourceData &s) { return s.live && s.paused; }));
        m_chipSource->setText(QStringLiteral("%1 源 · %2 暂停").arg(m_sourceStore.items().size()).arg(paused));
    }
    else if (m_sourceStore.items().size() == 1)
        m_chipSource->setText(QStringLiteral("%1 · %2 通道")
                                  .arg(m_sourceStore.items().first().fmt)
                                  .arg(channels));
    else
        m_chipSource->setText(QStringLiteral("%1 源 · %2 通道")
                                  .arg(m_sourceStore.items().size())
                                  .arg(channels));
}

void MainWindow::renderSourceData()
{
    updateStatusChips();
    m_dataSourceDock->renderSourceData(m_srcChannels, m_srcParams, m_srcMsgs,
                                      m_srcFmt, m_viewBacked, m_activeSource);
}

int MainWindow::sourceIdForRow(QWidget *row) const
{
    return row ? row->property("srcId").toInt() : -1;
}

ps::data_source::SourceData *MainWindow::sourceById(int id)
{
    return m_sourceStore.byId(id);
}

namespace {
// ULog 自带 'L'/'C' 文本日志（有真实事件时间），比按 topic 首样本聚合更有意义。
QVector<QStringList> ulogLogMessages(const QString &path, double base)
{
    QVector<QStringList> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray buf = f.readAll();
    if (buf.size() < 24 || std::memcmp(buf.constData(), "ULog", 4) != 0)
        return out;
    int pos = 16;
    while (pos + 3 <= buf.size())
    {
        const quint16 size = qFromLittleEndian<quint16>(buf.constData() + pos);
        const char ty = buf.at(pos + 2);
        const int body = pos + 3;
        if (body + size > buf.size())
            break;
        const char *p = buf.constData() + body;
        if (ty == 'L' && size >= 9)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 1);
            QByteArray text(p + 9, size - 9);
            const int nul = text.indexOf('\0');
            if (nul >= 0)
                text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        else if (ty == 'C' && size >= 11)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 3);
            QByteArray text(p + 11, size - 11);
            const int nul = text.indexOf('\0');
            if (nul >= 0)
                text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        pos = body + size;
    }
    return out;
}

bool sourceTimeRange(const ps::data_source::SourceData *s, double &t0, double &t1)
{
    bool init = false;
    for (const PsChannelInfo *ch : s->channels)
    {
        if (!ch || ch->point_count < 2 || !ch->time)
            continue;
        const double a = ch->time[0];
        const double b = ch->time[ch->point_count - 1];
        if (!init) { t0 = a; t1 = b; init = true; }
        else { t0 = std::min(t0, a); t1 = std::max(t1, b); }
    }
    return init;
}
} // namespace

void MainWindow::setSourceOffset(ps::data_source::SourceData *src, double offsetSec, bool refit)
{
    if (!src)
        return;
    src->offsetSec = offsetSec;
    for (auto *c : findChildren<CanvasWidget *>())
        c->setSourceOffset(src->path, offsetSec, refit);
    if (auto *row = src->row)
        row->setToolTip(QStringLiteral("%1 · 显示偏移 %2 s").arg(src->alias).arg(offsetSec, 0, 'f', 3));
}

void MainWindow::alignSource(ps::data_source::SourceData *src, int mode)
{
    if (!src)
        return;
    double a0 = 0, a1 = 0;
    if (!sourceTimeRange(src, a0, a1))
        return;
    bool init = false;
    double gs = 0, ge = 0;
    for (const ps::data_source::SourceData &o : m_sourceStore.items())
    {
        if (o.id == src->id)
            continue;
        double b0 = 0, b1 = 0;
        if (!sourceTimeRange(&o, b0, b1))
            continue;
        const double ds = b0 - o.offsetSec;
        const double de = b1 - o.offsetSec;
        if (!init) { gs = ds; ge = de; init = true; }
        else { gs = std::min(gs, ds); ge = std::max(ge, de); }
    }
    if (!init)
        return;
    const double target = mode == 0 ? gs : (mode == 2 ? ge : (gs + ge) / 2.0);
    const double center = (a0 + a1) / 2.0;
    const double off = mode == 0 ? a0 - target : (mode == 2 ? a1 - target : center - target);
    setSourceOffset(src, off, true);
    log(QStringLiteral("对齐 %1：%2，偏移 %3 s")
            .arg(src->alias, mode == 0 ? QStringLiteral("起点")
                                       : (mode == 2 ? QStringLiteral("终点") : QStringLiteral("中心")))
            .arg(off, 0, 'f', 3));
}

void MainWindow::setSourceBase(ps::data_source::SourceData *src)
{
    double a0 = 0, a1 = 0;
    if (!src || !sourceTimeRange(src, a0, a1))
        return;
    // 基准源的起点为 0，其余源用同一基准换算（PJ globalTimeReference 语义）
    for (ps::data_source::SourceData &s : m_sourceStore.items())
        setSourceOffset(&s, a0, false);
    for (auto *c : findChildren<CanvasWidget *>())
        for (const ps::data_source::SourceData &s : m_sourceStore.items())
            c->setSourceOffset(s.path, a0, true);
    log(QStringLiteral("时间基准：%1，起点归零，其余源同步换算").arg(src->alias));
}

void MainWindow::compareSources(ps::data_source::SourceData *src, ps::data_source::SourceData *other, bool all, const QString &pick)
{
    if (!src || !other || !currentCanvas())
        return;
    QStringList common;
    for (const PsChannelInfo *a : src->channels)
    {
        if (!a || a->point_count < 2)
            continue;
        const QString n = QString::fromUtf8(a->name);
        for (const PsChannelInfo *b : other->channels)
            if (b && b->point_count > 1 && n == QString::fromUtf8(b->name))
            { common.append(n); break; }
    }
    common.sort();
    if (common.isEmpty())
    {
        log(QStringLiteral("%1 与 %2 没有同名通道").arg(src->alias, other->alias));
        return;
    }
    QStringList names = all ? common : QStringList{pick};
    if (!all)
    {
        bool ok = false;
        const QString n = QInputDialog::getItem(this, QStringLiteral("对比同名通道"),
                                                QStringLiteral("%1 ↔ %2").arg(src->alias, other->alias),
                                                common, qMax(0, common.indexOf(pick)), false, &ok);
        if (!ok || n.isEmpty())
            return;
        names = {n};
    }
    auto *c = currentCanvas();
    int added = 0;
    for (const QString &n : names)
    {
        const PsChannelInfo *a = nullptr;
        const PsChannelInfo *b = nullptr;
        for (const PsChannelInfo *x : src->channels)
            if (x && n == QString::fromUtf8(x->name)) { a = x; break; }
        for (const PsChannelInfo *x : other->channels)
            if (x && n == QString::fromUtf8(x->name)) { b = x; break; }
        if (!a || !b)
            continue;
        c->addChannelEx(a, src->id, src->path, src->alias, src->offsetSec);
        c->addChannelEx(b, other->id, other->path, other->alias, other->offsetSec);
        added += 2;
    }
    log(QStringLiteral("对比 %1 ↔ %2：%3 条曲线").arg(src->alias, other->alias).arg(added));
}







void MainWindow::openSourceDebug(ps::data_source::SourceData *src)
{
    if (!src || !src->live)
        return;
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src->sourceId);
    if (!sp || !sp->send)
    {
        QMessageBox::information(this, QStringLiteral("调试"),
                                 QStringLiteral("该实时源未提供命令通道。"));
        return;
    }
    QVariantMap args;
    args.insert(QStringLiteral("sourceId"), sp->id);
    args.insert(QStringLiteral("name"), src->name);
    args.insert(QStringLiteral("handle"),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(src->handle)));
    QDialog *dlg = m_extensions.registry().createDialog(QStringLiteral("dialog.device_debug"),
                                                       args, this);
    if (!dlg)
        return;
    dlg->exec();
    delete dlg;
}









void MainWindow::updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor)
{
    if (!row)
        return;
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    if (auto *label = row->findChild<QLabel *>(QStringLiteral("srcMeta")))
        label->setText(meta);
    if (auto *dot = row->findChild<QLabel *>(QStringLiteral("srcDot")))
        dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    refreshSrcCount();
}

void MainWindow::addTransformFor(const QString &channel)
{
    ps::data_source::SourceData *src = sourceById(m_activeSource);
    if (!src || !src->ds)
        return;
    QVariantMap args;
    args.insert(QStringLiteral("dataset"),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(src->ds)));
    QVariantList channels;
    for (const PsChannelInfo *ch : src->channels)
        channels.append(QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(ch)));
    args.insert(QStringLiteral("channels"), channels);
    args.insert(QStringLiteral("sourceName"), src->alias.isEmpty() ? src->name : src->alias);
    args.insert(QStringLiteral("preselect"), channel);
    QDialog *dlg = m_extensions.registry().createDialog(QStringLiteral("dialog.transform"), args, this);
    if (!dlg)
        return;
    const bool accepted = dlg->exec() == QDialog::Accepted;
    auto *transform = dynamic_cast<ITransformDialog *>(dlg);
    if (!accepted || !transform)
    {
        delete dlg;
        return;
    }
    const TransformDialogResult result = transform->transformResult();
    delete dlg;

    for (const QString &n : result.addedNames)
        if (!src->derived.contains(n))
            src->derived.append(n);
    TransformRecipe r;
    r.kind = result.kind;
    r.input = result.inputName;
    r.filter = result.filterId;
    r.params = result.params;
    r.output = result.outputName;
    r.quat[0] = result.quat[0];
    r.quat[1] = result.quat[1];
    r.quat[2] = result.quat[2];
    r.quat[3] = result.quat[3];
    r.degrees = result.degrees;
    r.unwrap = result.unwrap;
    src->recipes.append(r);
    refreshActiveSource();

    QVector<const PsChannelInfo *> out;
    for (const PsChannelInfo *ch : src->channels)
        if (result.addedNames.contains(QString::fromUtf8(ch->name)))
            out.append(ch);
    if (out.isEmpty())
        return;
    bool replaced = false;
    if (!result.inputName.isEmpty())
        for (auto *c : findChildren<CanvasWidget *>())
            replaced = c->replaceChannel(result.inputName, out.first()) || replaced;
    if (!replaced)
        for (const PsChannelInfo *ch : out)
            if (auto *c = currentCanvas())
                c->addChannel(ch);
    log(QStringLiteral("变换完成：%1").arg(result.addedNames.join(QStringLiteral(", "))));
}







QWidget *MainWindow::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    return m_dataSourceDock->addSourceRow(name, meta, dotColor, backed, path, srcId);
}

void MainWindow::refreshSrcCount()
{
    m_dataSourceDock->refreshSrcCount();
}

void MainWindow::syncSrcChips()
{
    m_dataSourceDock->syncSrcChips();
}

void MainWindow::updateSrcStatus()
{
    m_dataSourceDock->updateSrcStatus();
}





void MainWindow::switchSource(int id) { m_dataSourceDock->switchSource(id); }
void MainWindow::reloadChannels(SourceData *src) { m_dataSourceDock->reloadChannels(src); }
void MainWindow::refreshActiveSource() { m_dataSourceDock->refreshActiveSource(); }
void MainWindow::activateSource(QWidget *row) { m_dataSourceDock->activateSource(row); }
void MainWindow::setSourcePaused(SourceData *src, bool paused) { m_dataSourceDock->setSourcePaused(src, paused); }
void MainWindow::exportActiveCsv() { m_dataSourceDock->exportActiveCsv(); }

void MainWindow::showSourceProps(QWidget *row)
{
    if (!row || !m_propTable)
        return;
    ps::data_source::SourceData *src = sourceById(sourceIdForRow(row));
    if (!src)
        return;
    const QString name = src->alias.isEmpty() ? src->name : src->alias;
    int channels = src->live
        ? (src->liveSession ? src->liveSession->channels().size() : 0)
        : src->channels.size();
    qint64 points = 0;
    double duration = 0.0;
    double rate = 0.0;
    if (src->live)
    {
        points = src->liveSession ? src->liveSession->totalSamples() : 0;
    }
    else
    {
        for (const PsChannelInfo *ch : src->channels)
            points += ch->point_count;
    }
    const QVector<const PsChannelInfo *> view =
        src->live ? (src->liveSession ? src->liveSession->channels() : QVector<const PsChannelInfo *>())
                  : src->channels;
    if (!view.isEmpty() && view.first()->point_count > 1)
    {
        const PsChannelInfo *ch = view.first();
        duration = ch->time[ch->point_count - 1] - ch->time[0];
        if (duration > 0.0)
            rate = (ch->point_count - 1) / duration;
    }
    const QString state = row->property("srcMeta").toString();
    const QFileInfo fi(src->path);
    QVector<QPair<QString, QString>> rows;
    rows.append({QStringLiteral("类型"), src->live ? QStringLiteral("实时源") : src->fmt});
    rows.append({QStringLiteral("名称"), name});
    rows.append({QStringLiteral("状态"), state.isEmpty() ? QStringLiteral("—") : state});
    rows.append({QStringLiteral("位置"), src->path.isEmpty() ? src->config : src->path});
    if (src->live)
        rows.append({QStringLiteral("扩展"), src->sourceId});
    rows.append({QStringLiteral("通道"), QString::number(channels)});
    rows.append({QStringLiteral("参数"), QString::number(src->params.size())});
    rows.append({QStringLiteral("消息"), QString::number(src->msgs.size())});
    rows.append({QStringLiteral("采样点"), QString::number(points)});
    if (duration > 0.0)
    {
        rows.append({QStringLiteral("时长"), QStringLiteral("%1 s").arg(duration, 0, 'f', 2)});
        rows.append({QStringLiteral("速率"),
                     rate >= 1000.0 ? QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'f', 1)
                                    : QStringLiteral("%1 Hz").arg(rate, 0, 'f', rate >= 10.0 ? 0 : 1)});
    }
    rows.append({QStringLiteral("时间偏移"), QStringLiteral("%1 s").arg(src->offsetSec, 0, 'f', 3)});
    if (fi.exists() && fi.size() > 0)
        rows.append({QStringLiteral("文件大小"),
                     QStringLiteral("%1 MB").arg(fi.size() / 1024.0 / 1024.0, 0, 'f', 2)});
    if (src->live)
        rows.append({QStringLiteral("录制"),
                     src->liveSession && src->liveSession->recording()
                         ? QStringLiteral("进行中 · %1").arg(src->liveSession->recordPath())
                         : QStringLiteral("未录制")});
    m_propTreeBuilding = true;
    if (m_propTree)
    {
        m_propTree->setCurrentItem(nullptr);
        m_propTree->clearSelection();
    }
    m_propTreeBuilding = false;
    setPropRows(QStringLiteral("数据源 · %1").arg(name), rows);
    if (m_propTable->rowCount() > 0 && m_propTable->item(0, 1))
        m_propTable->item(0, 1)->setToolTip(rows.first().second);
}

void MainWindow::addTimeMarker(double t, const QString &label, bool prompt)
{
    QString text = label.trimmed();
    if (prompt)
    {
        bool ok = false;
        QString def = text.isEmpty() ? QStringLiteral("标签") : text;
        text = QInputDialog::getText(this, QStringLiteral("添加时间标签"),
                                     QStringLiteral("时间 %1 s\n标签").arg(t, 0, 'f', 2),
                                     QLineEdit::Normal, def, &ok);
        if (!ok)
            return;
        text = text.trimmed();
        if (text.isEmpty())
            text = def;
    }
    if (text.isEmpty())
        text = QStringLiteral("%1 s").arg(t, 0, 'f', 2);
    for (auto &mk : m_timeMarkers)
        if (qAbs(mk.first - t) < 1e-4)
        {
            mk.second = text;
            applyTimeMarkers();
            log(QStringLiteral("更新时间标签：%1 @ %2 s").arg(text).arg(t, 0, 'f', 3));
            return;
        }
    m_timeMarkers.append({t, text});
    std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
              [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
                  return a.first < b.first;
              });
    applyTimeMarkers();
    log(QStringLiteral("添加时间标签：%1 @ %2 s").arg(text).arg(t, 0, 'f', 3));
}

void MainWindow::removeTimeMarker(double t)
{
    for (int i = 0; i < m_timeMarkers.size(); ++i)
    {
        if (qAbs(m_timeMarkers.at(i).first - t) < 1e-4)
        {
            const QString label = m_timeMarkers.at(i).second;
            m_timeMarkers.removeAt(i);
            applyTimeMarkers();
            log(QStringLiteral("删除时间标签：%1").arg(label));
            return;
        }
    }
}

void MainWindow::applyTimeMarkers()
{
    if (m_plotWorkspace)
        m_plotWorkspace->setTimeMarkers(m_timeMarkers);
}

void MainWindow::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    // 源内联数据列：源始终显示，按组/叶子过滤
    if (QWidget *host = m_srcRows ? m_srcRows->parentWidget() : nullptr)
        for (auto *panel : host->findChildren<ChannelTree *>())
        {
            if (!panel->property("srcPanel").toBool())
                continue;
            for (int i = 0; i < panel->topLevelItemCount(); ++i)
            {
                QTreeWidgetItem *g = panel->topLevelItem(i);
                const bool groupHit = q.isEmpty() || g->text(0).contains(q, Qt::CaseInsensitive);
                int hits = 0;
                for (int j = 0; j < g->childCount(); ++j)
                {
                    QTreeWidgetItem *leaf = g->child(j);
                    const bool hit = groupHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                    leaf->setHidden(!hit);
                    if (hit)
                        ++hits;
                }
                g->setHidden(q.isEmpty() ? false : (!groupHit && hits == 0));
                if (!q.isEmpty() && hits > 0)
                    g->setExpanded(true);
            }
        }
    if (tab == 0 && m_srcTree)
    {
        for (int i = 0; i < m_srcTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *topic = m_srcTree->topLevelItem(i);
            const bool topicHit = q.isEmpty() || topic->text(0).contains(q, Qt::CaseInsensitive);
            int childHit = 0;
            for (int j = 0; j < topic->childCount(); ++j)
            {
                QTreeWidgetItem *leaf = topic->child(j);
                const bool hit = topicHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                if (hit)
                    ++childHit;
            }
            topic->setHidden(q.isEmpty() ? false : (!topicHit && childHit == 0));
            if (!q.isEmpty() && childHit > 0)
                topic->setExpanded(true);
        }
    }
    else if (tab == 1 && m_paramTable)
    {
        for (int r = 0; r < m_paramTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_paramTable->columnCount() && !hit; ++c)
                hit = m_paramTable->item(r, c) && m_paramTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_paramTable->setRowHidden(r, !hit);
        }
    }
    else if (tab == 2 && m_msgTable)
    {
        for (int r = 0; r < m_msgTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_msgTable->columnCount() && !hit; ++c)
                hit = m_msgTable->item(r, c) && m_msgTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_msgTable->setRowHidden(r, !hit);
        }
    }
}

void MainWindow::newPage()
{
    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(QStringLiteral("新页 · 画布"));
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, QStringLiteral("新页"));
    m_pages->setCurrentWidget(c);
    refreshPropTree(c);
    log(QStringLiteral("新建页"));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { repositionDockHandles(this); });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (ps::data_source::SourceData &src : m_sourceStore.items())
        closeLiveSource(&src);
    if (m_liveTimer)
        m_liveTimer->stop();
    if (QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
            .value(QStringLiteral("restoreLayout"), true).toBool())
        saveLayoutTo(defaultLayoutPath());
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 数据源行：点行切换该源自己的字段/参数/消息
    if (event->type() == QEvent::MouseButtonRelease && watched->property("srcRow").toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *row = qobject_cast<QWidget *>(watched);
        activateSource(row);
        if (me->button() == Qt::RightButton)
            showSourceMenu(row, me->globalPosition().toPoint());
        return false;
    }
    // Ribbon 按钮的自定义 tooltip：开着它会在鼠标下方盖住分组标题，改走状态栏
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        if (qobject_cast<SARibbonToolButton *>(watched))
        {
            if (event->type() == QEvent::Enter)
            {
                const QString tip = watched->property("rbTip").toString();
                if (!tip.isEmpty())
                    statusBar()->showMessage(tip);
            }
            else
            {
                statusBar()->showMessage(m_kernelInfo);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createPages()
{
    m_plotWorkspace = new ps::plot::PlotWorkspace(
        [](QWidget *parent) { return new CanvasWidget(parent); }, this);
    m_pages = m_plotWorkspace;
    m_plotWorkspace->createDefaultPages();
    m_plotWorkspace->setTimeMarkers(m_timeMarkers);

    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasActivated, this,
            [this](CanvasWidget *c) { refreshPropTree(c); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasAddRequested, this,
            [this](CanvasWidget *) { addCanvasToPage(); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasSplitRequested, this,
            [this](CanvasWidget *c, int orientation) {
                splitCanvas(c, orientation == 0 ? Qt::Horizontal : Qt::Vertical);
            });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::addChannelRequested, this,
            [this](CanvasWidget *c) {
                if (m_srcChannels.isEmpty())
                {
                    log(QStringLiteral("先加载数据文件，再添加曲线"));
                    return;
                }
                QStringList names;
                for (const PsChannelInfo *ch : m_srcChannels)
                {
                    const QString n = QString::fromUtf8(ch->name);
                    names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
                }
                bool ok = false;
                const QString pick = QInputDialog::getItem(this, QStringLiteral("添加曲线"),
                                                           QStringLiteral("通道"), names, 0, false, &ok);
                if (ok && !pick.isEmpty())
                {
                    c->addChannel(m_srcChannels.value(names.indexOf(pick)));
                    log(QStringLiteral("添加曲线：%1").arg(pick));
                }
            });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasMaximizeRequested, this,
            [this](CanvasWidget *c) { toggleCanvasMaximize(c); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRequested, this,
            [this](double t) { addTimeMarker(t, QString(), true); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRemoveRequested, this,
            [this](double t) { removeTimeMarker(t); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasRemoveRequested, this,
            [this](CanvasWidget *c) { removeCanvas(c); });

    connect(m_pages, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_pages->count() > 2 && !m_pages->tabText(idx).isEmpty())
            m_pages->removeTab(idx);
    });

    setCentralWidget(m_pages);
    connect(m_pages, &QTabWidget::currentChanged, this, [this](int i) {
        if (m_maxedCanvas)
            restoreCanvasMaximize();
        refreshPropTree(currentCanvas());
        if (i >= 0 && i == m_pages->count() - 1 && m_pages->tabText(i) == QStringLiteral("＋"))
            newPage();
    });
}

void MainWindow::applyCanvasGrid(int rows, int cols)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || dynamic_cast<QLabel *>(page))
    {
        log(QStringLiteral("当前页不是绘图页"));
        return;
    }
    QVector<CanvasWidget *> canvases;
    collectPageCanvases(page, canvases);
    if (canvases.isEmpty())
    {
        log(QStringLiteral("当前页没有画布"));
        return;
    }

    const int idx = m_pages->currentIndex();
    const QString tabText = m_pages->tabText(idx);
    auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
    m_pages->removeTab(idx);
    m_pages->insertTab(idx, root, tabText);
    m_pages->setCurrentIndex(idx);
    page->deleteLater();
    log(QStringLiteral("页内布局：%1 × %2（可继续拆分）").arg(rows).arg(cols));
}

void MainWindow::toggleCanvasMaximize(CanvasWidget *c)
{
    if (m_maxedCanvas == c)
    {
        restoreCanvasMaximize();
        return;
    }
    if (m_maxedCanvas)
        restoreCanvasMaximize();
    if (!c || !m_pages || !m_pages->isAncestorOf(c))
        return;
    m_sizeBeforeMax = size();

    QWidget *page = pageForCanvas(c);
    if (!page)
        return;

    // 逐级隐藏同级兄弟：画布填满当前页，不脱离原有 splitter 树。
    QWidget *child = c;
    while (child && child != page)
    {
        QSplitter *sp = qobject_cast<QSplitter *>(child->parentWidget());
        if (!sp)
            break;
        m_maxedSizes.append({sp, sp->sizes()});
        for (int i = 0; i < sp->count(); ++i)
        {
            QWidget *w = sp->widget(i);
            if (w && w != child && !w->isHidden())
            {
                w->hide();
                m_maxedHidden.append(w);
            }
        }
        child = sp;
    }
    m_maxedCanvas = c;
    c->setMaximizedState(true);
    log(QStringLiteral("最大化画布：%1").arg(c->title()));
}

void MainWindow::restoreCanvasMaximize()
{
    if (!m_maxedCanvas)
        return;
    CanvasWidget *c = m_maxedCanvas;
    m_maxedCanvas = nullptr;
    for (QWidget *w : m_maxedHidden)
        if (w)
            w->show();
    for (const auto &p : m_maxedSizes)
        if (p.first)
            p.first->setSizes(p.second);
    m_maxedHidden.clear();
    m_maxedSizes.clear();
    c->setMaximizedState(false);
    // 隐藏/显示画布会触发 QMainWindow 重算 sizeHint，恢复时把窗口尺寸拉回去。
    const QSize sz = m_sizeBeforeMax;
    QTimer::singleShot(0, this, [this, sz] { if (sz.isValid()) resize(sz); });
}

void MainWindow::splitCanvas(CanvasWidget *target, Qt::Orientation orientation)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || !target)
        return;
    if (dynamic_cast<QLabel *>(page))
        return;

    // 网格只是“一次摆好”的快捷方式；拆分前先变成 splitter 树，之后就能任意再分
    if (dynamic_cast<PlotGrid *>(page) || !target->parentWidget() ||
        (target->parentWidget() != page && !qobject_cast<QSplitter *>(target->parentWidget())))
    {
        QVector<CanvasWidget *> canvases;
        collectPageCanvases(page, canvases);
        const int rows = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->rows() : 1;
        const int cols = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->cols() : 1;
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, root, tabText);
        m_pages->setCurrentIndex(idx);
        page->deleteLater();
        page = root;
    }

    auto *c = new CanvasWidget(m_pages);
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    c->setTimeRange(target->t0(), target->t1());
    m_plotWorkspace->attachCanvas(c);

    if (target->parentWidget() == page && qobject_cast<CanvasWidget *>(page) == target)
    {
        auto *sp = new QSplitter(orientation, m_pages);
        sp->setHandleWidth(5);
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, sp, tabText);
        m_pages->setCurrentIndex(idx);
        sp->addWidget(target);
        sp->addWidget(c);
        QTimer::singleShot(0, sp, [sp] {
            QList<int> eq;
            for (int i = 0; i < sp->count(); ++i)
                eq << 1;
            sp->setSizes(eq);
        });
        log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                              : QStringLiteral("上下")));
        return;
    }

    auto *sp = qobject_cast<QSplitter *>(target->parentWidget());
    if (!sp)
        return;
    const int at = sp->indexOf(target);
    if (sp->orientation() == orientation)
    {
        sp->insertWidget(at + 1, c);
    }
    else
    {
        auto *nested = new QSplitter(orientation, sp);
        nested->setHandleWidth(5);
        sp->insertWidget(at, nested);
        nested->addWidget(target);
        nested->addWidget(c);
        nested->setStretchFactor(0, 1);
        nested->setStretchFactor(1, 1);
        QTimer::singleShot(0, nested, [nested] { nested->setSizes({1, 1}); });
    }
    log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                          : QStringLiteral("上下")));
    refreshPropTree(c);
}

void MainWindow::createPlotPage(const QString &kind)
{
    struct PlotKind
    {
        const char *kind;
        const char *label;
        CanvasWidget::PlotMode mode;
    };
    static const PlotKind kinds[] = {
        {"timeseries", "时间序列", CanvasWidget::TimeSeries},
        {"xy", "XY 曲线", CanvasWidget::Xy},
        {"scatter", "散点图", CanvasWidget::Scatter},
        {"bar", "柱状图", CanvasWidget::BarChart},
        {"histogram", "直方图", CanvasWidget::Histogram},
        {"fft", "频谱 FFT", CanvasWidget::Spectrum},
        {"traj3d", "3D 轨迹", CanvasWidget::Traj3D},
        {"map", "GPS 地图", CanvasWidget::GpsMap},
        {"surface3d", "3D 曲面", CanvasWidget::Surface3D},
        {"horizon", "地平仪", CanvasWidget::Horizon},
        {"compass", "罗盘", CanvasWidget::Compass},
        {"gauge", "表盘", CanvasWidget::Gauge},
    };
    const PlotKind *spec = nullptr;
    for (const PlotKind &k : kinds)
        if (kind == QLatin1String(k.kind))
            spec = &k;
    if (!spec)
    {
        log(QStringLiteral("未知图型：%1").arg(kind));
        return;
    }
    const QString label = QString::fromUtf8(spec->label);
    if (spec->mode == CanvasWidget::TimeSeries)
    {
        newPage();
        if (auto *c = currentCanvas())
        {
            c->setTitle(QStringLiteral("%1 · 选择通道").arg(label));
            log(QStringLiteral("新建图页：%1").arg(label));
        }
        return;
    }

    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(label);
    c->setPlotMode(spec->mode, kind);
    CanvasWidget *seed = nullptr;
    int best = 0;
    for (CanvasWidget *w : findChildren<CanvasWidget *>())
    {
        if (w == c)
            continue;
        const int n = w->sampleSeries().size();
        if (n > best)
        {
            best = n;
            seed = w;
        }
    }
    if (seed)
        for (const CanvasWidget::SampleSeries &s : seed->sampleSeries())
            c->addGeneratedSeries(s.name, s.time, s.value);
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, label);
    m_pages->setCurrentWidget(c);
    log(QStringLiteral("新建图页：%1").arg(label));
}

void MainWindow::addCanvasToPage()
{
    QWidget *page = m_pages->currentWidget();
    if (qobject_cast<QLabel *>(page))
    {
        log(QStringLiteral("该页不是绘图页，先切到绘图页再加画布"));
        return;
    }

    CanvasWidget *target = currentCanvas();
    if (!target)
        target = pageCanvases().value(0, nullptr);
    if (target)
        splitCanvas(target, Qt::Vertical);
    else
        log(QStringLiteral("当前页没有可拆分的画布"));
}

void MainWindow::removeCanvas(CanvasWidget *c)
{
    if (m_maxedCanvas == c)
        restoreCanvasMaximize();
    if (!c)
        return;
    if (auto *sp = qobject_cast<QSplitter *>(c->parentWidget()))
    {
        if (sp->count() <= 1)
        {
            log(QStringLiteral("页内至少保留一个画布"));
            return;
        }
        c->deleteLater();
        log(QStringLiteral("删除画布"));
        refreshPropTree(currentCanvas());
        return;
    }
    log(QStringLiteral("页内至少保留一个画布"));
}

QString MainWindow::defaultLayoutPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    return dir + QStringLiteral("/last.psl");
}

QJsonObject MainWindow::pageState(QWidget *page) const
{
    QJsonObject o;
    if (auto *c = qobject_cast<CanvasWidget *>(page))
    {
        o = c->saveState();
        o[QStringLiteral("type")] = QStringLiteral("canvas");
    }
    else if (auto *g = dynamic_cast<PlotGrid *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("grid");
        o[QStringLiteral("rows")] = g->rows();
        o[QStringLiteral("cols")] = g->cols();
        QJsonArray kids;
        for (CanvasWidget *c : g->canvases())
        {
            QJsonObject co = c->saveState();
            co[QStringLiteral("type")] = QStringLiteral("canvas");
            kids.append(co);
        }
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("splitter");
        o[QStringLiteral("vertical")] = sp->orientation() == Qt::Vertical;
        QJsonArray sizes;
        for (int v : sp->sizes())
            sizes.append(v);
        o[QStringLiteral("sizes")] = sizes;
        QJsonArray kids;
        for (int i = 0; i < sp->count(); ++i)
            kids.append(pageState(sp->widget(i)));
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *lb = qobject_cast<QLabel *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("label");
        o[QStringLiteral("text")] = lb->text();
    }
    return o;
}

QWidget *MainWindow::buildPage(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new CanvasWidget(m_pages);
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const ps::data_source::SourceData &src : m_sourceStore.items())
        {
            for (const PsChannelInfo *ch : src.channels)
            {
                const QString name = QString::fromUtf8(ch->name);
                pool.insert(src.path + QLatin1Char('|') + name, ch);
                if (!pool.contains(name))
                    pool.insert(name, ch);
            }
        }
        c->restoreState(o, pool);
        m_plotWorkspace->attachCanvas(c);
        return c;
    }
    if (type == QLatin1String("grid"))
    {
        auto *g = new PlotGrid(m_pages);
        g->setShape(o.value(QStringLiteral("rows")).toInt(1), o.value(QStringLiteral("cols")).toInt(1));
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            if (auto *c = qobject_cast<CanvasWidget *>(buildPage(v.toObject())))
                g->addCanvas(c);
        return g;
    }
    if (type == QLatin1String("splitter"))
    {
        auto *sp = new QSplitter(o.value(QStringLiteral("vertical")).toBool(true) ? Qt::Vertical
                                                                                 : Qt::Horizontal,
                                 m_pages);
        sp->setHandleWidth(6);
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            sp->addWidget(buildPage(v.toObject()));
        QList<int> sizes;
        for (const QJsonValue &v : o.value(QStringLiteral("sizes")).toArray())
            sizes.append(v.toInt());
        if (!sizes.isEmpty())
            sp->setSizes(sizes);
        return sp;
    }
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_pages);
}

void MainWindow::runScript(bool apply)
{
    QDockWidget *scriptDock = findChild<QDockWidget *>(QStringLiteral("dock.script"));
    CanvasWidget *canvas = nullptr;
    QVector<ScriptSeries> inputs;
    QVector<CanvasWidget *> candidates = pageCanvases();
    if (candidates.isEmpty())
        candidates = findChildren<CanvasWidget *>();
    for (CanvasWidget *c : candidates)
    {
        const auto samples = c->sampleSeries();
        if (samples.isEmpty() || (canvas && inputs.size() >= samples.size()))
            continue;
        canvas = c;
        inputs.clear();
        for (const CanvasWidget::SampleSeries &s : samples)
            inputs.append({s.name, s.time, s.value});
    }
    if (!canvas)
    {
        log(QStringLiteral("脚本：当前页没有可见曲线"));
        return;
    }
    QStringList names;
    for (const ScriptSeries &s : inputs)
        names.append(s.name);
    for (QComboBox *cb : m_scriptInputs)
    {
        if (!cb)
            continue;
        const QString keep = cb->currentData().toString();
        cb->clear();
        cb->addItem(QStringLiteral("（未绑定）"), QString());
        for (const QString &name : names)
            cb->addItem(name, name);
        cb->setCurrentIndex(qMax(0, cb->findData(keep)));
    }

    QVector<ScriptSeries> bound;
    QStringList boundNames;
    for (QComboBox *cb : m_scriptInputs)
    {
        const QString name = cb ? cb->currentData().toString() : QString();
        if (name.isEmpty())
            continue;
        for (const ScriptSeries &s : inputs)
        {
            if (s.name != name)
                continue;
            bound.append(s);
            boundNames.append(QStringLiteral("v%1=%2").arg(bound.size()).arg(name));
            break;
        }
    }
    if (bound.isEmpty())
        bound = inputs;
    log(QStringLiteral("脚本输入：%1").arg(
        boundNames.isEmpty() ? names.join(QStringLiteral(" | ")) : boundNames.join(QStringLiteral(" | "))));
    QMap<QString, double> params;
    if (m_scriptParams)
    {
        for (int row = 0; row < m_scriptParams->rowCount(); ++row)
        {
            const QString key = m_scriptParams->item(row, 0)
                                    ? m_scriptParams->item(row, 0)->text().trimmed()
                                    : QString();
            if (key.isEmpty())
                continue;
            bool ok = false;
            const double v = m_scriptParams->item(row, 1)
                                 ? m_scriptParams->item(row, 1)->text().toDouble(&ok)
                                 : 0.0;
            if (ok)
                params.insert(key, v);
        }
    }
    const ScriptResult r = m_extensions.registry().runScript(m_script->toPlainText(), bound, params);
    if (!r.ok)
    {
        log(QStringLiteral("脚本失败：%1").arg(r.error));
        const QRegularExpression re(QStringLiteral(":(\\d+):"));
        const auto match = re.match(r.error);
        if (match.hasMatch())
        {
            const int line = match.captured(1).toInt();
            QTextCursor cur = m_script->textCursor();
            cur.movePosition(QTextCursor::Start);
            cur.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, qMax(0, line - 1));
            cur.select(QTextCursor::LineUnderCursor);
            m_script->setTextCursor(cur);
        }
        if (scriptDock)
        {
            scriptDock->show();
            scriptDock->raise();
        }
        return;
    }
    if (m_scriptPreviewCanvas)
        for (const QString &name : m_scriptPreviewNames)
            m_scriptPreviewCanvas->removeGeneratedSeries(name);
    m_scriptPreviewCanvas = canvas;
    m_scriptPreviewNames.clear();

    if (apply)
    {
        int idx = 0;
        for (const ScriptOutput &o : r.outputs)
        {
            const QString source = idx < bound.size() ? bound[idx].name : QString();
            ++idx;
            if (!source.isEmpty() && canvas->replaceGeneratedSeries(source, o.name, o.time, o.value))
                log(QStringLiteral("Lua 应用：%1 → %2（继承颜色，%3 点）")
                        .arg(source, o.name).arg(o.value.size()));
            else
            {
                canvas->addGeneratedSeries(o.name, o.time, o.value);
                log(QStringLiteral("Lua 应用：%1（%2 点）").arg(o.name).arg(o.value.size()));
            }
        }
    }
    else
    {
        for (const ScriptOutput &o : r.outputs)
        {
            const QString name = QStringLiteral("preview/%1").arg(o.name);
            canvas->addGeneratedSeries(name, o.time, o.value);
            m_scriptPreviewNames.append(name);
            log(QStringLiteral("Lua 预览：%1（%2 点）").arg(name).arg(o.value.size()));
        }
    }
    log(QStringLiteral("Lua 5.4 %1：%2 条输出")
            .arg(apply ? QStringLiteral("应用完成") : QStringLiteral("预览完成"))
            .arg(r.outputs.size()));
}

void MainWindow::saveLayoutTo(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("PlotScope");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("geometry")] = QString::fromLatin1(saveGeometry().toBase64());
    root[QStringLiteral("docks")] = QString::fromLatin1(saveState().toBase64());
    root[QStringLiteral("current")] = m_pages->currentIndex();

    QJsonArray pages;
    QJsonArray titles;
    const int last = m_pages->count() - 1;
    for (int i = 0; i < m_pages->count(); ++i)
    {
        if (i == last && m_pages->tabText(i) == QStringLiteral("＋"))
            continue;
        pages.append(pageState(m_pages->widget(i)));
        titles.append(m_pages->tabText(i));
    }
    root[QStringLiteral("pages")] = pages;
    root[QStringLiteral("titles")] = titles;
    QJsonArray markers;
    for (const auto &mk : m_timeMarkers)
    {
        QJsonObject mo;
        mo[QStringLiteral("t")] = mk.first;
        mo[QStringLiteral("label")] = mk.second;
        markers.append(mo);
    }
    root[QStringLiteral("markers")] = markers;

    // 数据源与布局一起保存：重启后先重新加载日志，再回填画布通道
    // 数据源与布局一起保存：重启后按顺序重载全部源，再回填画布通道
    QJsonArray sources;
    for (const ps::data_source::SourceData &src : m_sourceStore.items())
    {
        if (src.path.isEmpty() || src.live)
            continue;
        QJsonObject so;
        so[QStringLiteral("path")] = src.path;
        so[QStringLiteral("name")] = src.name;
        so[QStringLiteral("alias")] = src.alias;
        so[QStringLiteral("offset")] = src.offsetSec;
        sources.append(so);
    }
    root[QStringLiteral("sources")] = sources;

    QJsonArray transforms;
    for (const ps::data_source::SourceData &src : m_sourceStore.items())
    {
        for (const ps::data_source::TransformRecipe &r : src.recipes)
        {
            QJsonObject o;
            o[QStringLiteral("source")] = src.path;
            o[QStringLiteral("kind")] = r.kind;
            o[QStringLiteral("input")] = r.input;
            o[QStringLiteral("filter")] = r.filter;
            o[QStringLiteral("params")] = r.params;
            o[QStringLiteral("output")] = r.output;
            o[QStringLiteral("degrees")] = r.degrees;
            o[QStringLiteral("unwrap")] = r.unwrap;
            if (r.kind == QLatin1String("quat"))
            {
                QJsonArray q;
                for (const QString &s : r.quat)
                    q.append(s);
                o[QStringLiteral("quat")] = q;
            }
            transforms.append(o);
        }
    }
    root[QStringLiteral("transforms")] = transforms;

    QJsonObject script;
    script[QStringLiteral("source")] = m_script ? m_script->toPlainText() : QString();
    QJsonArray binds;
    for (QComboBox *cb : m_scriptInputs)
        binds.append(cb ? cb->currentData().toString() : QString());
    script[QStringLiteral("inputs")] = binds;
    QJsonObject paramObj;
    if (m_scriptParams)
    {
        for (int row = 0; row < m_scriptParams->rowCount(); ++row)
        {
            const QString key = m_scriptParams->item(row, 0)
                                    ? m_scriptParams->item(row, 0)->text().trimmed()
                                    : QString();
            if (key.isEmpty())
                continue;
            bool ok = false;
            const double v = m_scriptParams->item(row, 1)
                                 ? m_scriptParams->item(row, 1)->text().toDouble(&ok)
                                 : 0.0;
            if (ok)
                paramObj[key] = v;
        }
    }
    script[QStringLiteral("params")] = paramObj;
    root[QStringLiteral("script")] = script;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log(QStringLiteral("保存布局失败：%1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已保存布局：%1").arg(path));
}

void MainWindow::importLayoutFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        log(QStringLiteral("导入布局失败：%1").arg(f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("PlotScope"))
    {
        log(QStringLiteral("不是 PlotScope 布局文件"));
        return;
    }

    // 先恢复数据源，buildPage 才能按通道名取到内核指针
    for (const QJsonValue &v : root.value(QStringLiteral("sources")).toArray())
    {
        const QString path = v.toObject().value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFile::exists(path))
            continue;
        bool loaded = false;
        for (int i = 0; m_srcRows && i < m_srcRows->count(); ++i)
        {
            QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
            if (row && row->property("srcPath").toString() == path)
            {
                loaded = true;
                break;
            }
        }
        if (!loaded)
            loadFile(path, false);
        const QString alias = v.toObject().value(QStringLiteral("alias")).toString();
        if (!alias.isEmpty())
        {
            for (ps::data_source::SourceData &src : m_sourceStore.items())
                if (src.path == path)
                    src.alias = alias;
        }
        const double offset = v.toObject().value(QStringLiteral("offset")).toDouble(0.0);
        for (ps::data_source::SourceData &src : m_sourceStore.items())
            if (src.path == path)
                setSourceOffset(&src, offset, false);
    }

    // 重放变换配方；派生通道先写回，buildPage 才能按名字找到
    for (const QJsonValue &v : root.value(QStringLiteral("transforms")).toArray())
    {
        const QJsonObject o = v.toObject();
        ps::data_source::SourceData *src = nullptr;
        for (ps::data_source::SourceData &s : m_sourceStore.items())
            if (s.path == o.value(QStringLiteral("source")).toString())
                src = &s;
        if (!src || !src->ds)
            continue;
        TransformRecipe r;
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.input = o.value(QStringLiteral("input")).toString();
        r.filter = o.value(QStringLiteral("filter")).toString();
        r.params = o.value(QStringLiteral("params")).toObject();
        r.output = o.value(QStringLiteral("output")).toString();
        r.degrees = o.value(QStringLiteral("degrees")).toBool(true);
        r.unwrap = o.value(QStringLiteral("unwrap")).toBool(true);
        const QJsonArray q = o.value(QStringLiteral("quat")).toArray();
        for (int i = 0; i < 4 && i < q.size(); ++i)
            r.quat[i] = q.at(i).toString();
        const int derivedCap = QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
                                   .value(QStringLiteral("derivedMaxPoints"), 2000000).toInt();
        if (derivedCap > 0)
        {
            QStringList ins = r.kind == QLatin1String("quat")
                                  ? QStringList{r.quat[0], r.quat[1], r.quat[2], r.quat[3]}
                                  : QStringList{r.input};
            uint32_t worst = 0;
            for (const QString &name : ins)
            {
                const uint32_t count = ps_dataset_channel_count(src->ds);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
                    if (ch && name == QString::fromUtf8(ch->name))
                    {
                        worst = qMax(worst, ch->point_count);
                        break;
                    }
                }
            }
            if (worst > static_cast<uint32_t>(derivedCap))
            {
                log(QStringLiteral("重放变换跳过：%1（%2 点 > 上限 %3）")
                        .arg(r.output).arg(worst).arg(derivedCap));
                continue;
            }
        }
        char err[512] = {0};
        int rc = 0;
        if (r.kind == QLatin1String("quat"))
            rc = ps_dataset_apply_quat_rpy(src->ds, r.quat[0].toUtf8().constData(), r.quat[1].toUtf8().constData(),
                                           r.quat[2].toUtf8().constData(), r.quat[3].toUtf8().constData(),
                                           r.output.toUtf8().constData(), r.degrees ? 1 : 0, r.unwrap ? 1 : 0,
                                           err, sizeof err);
        else
            rc = ps_dataset_apply_filter(
                src->ds, r.input.toUtf8().constData(), r.filter.toUtf8().constData(),
                QJsonDocument(r.params).toJson(QJsonDocument::Compact).constData(),
                r.output.toUtf8().constData(), err, sizeof err);
        if (rc != 0)
        {
            log(QStringLiteral("重放变换失败：%1（%2）").arg(r.output, QString::fromUtf8(err)));
            continue;
        }
        src->recipes.append(r);
        const QStringList names = r.kind == QLatin1String("quat")
                                      ? QStringList{r.output + QStringLiteral(".roll"),
                                                    r.output + QStringLiteral(".pitch"),
                                                    r.output + QStringLiteral(".yaw")}
                                      : QStringList{r.output};
        for (const QString &n : names)
            if (!src->derived.contains(n))
                src->derived.append(n);
        reloadChannels(src);
    }
    if (!m_sourceStore.items().isEmpty())
        refreshActiveSource();

    // 清空页签时屏蔽 currentChanged：末尾的 ＋ 触发 newPage 会和删除循环互踢
    m_pages->blockSignals(true);
    while (m_pages->count() > 0)
        m_pages->removeTab(0);
    m_pages->blockSignals(false);

    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    const QJsonArray titles = root.value(QStringLiteral("titles")).toArray();
    for (int i = 0; i < pages.size(); ++i)
        m_pages->addTab(buildPage(pages[i].toObject()),
                        titles.at(i).toString(QStringLiteral("页 %1").arg(i + 1)));

    const int plusIdx = m_pages->count();
    m_pages->addTab(new QWidget(m_pages), QStringLiteral("＋"));
    m_pages->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_pages->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);

    const QByteArray geo = QByteArray::fromBase64(root.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geo.isEmpty())
        restoreGeometry(geo);
    const QByteArray docks = QByteArray::fromBase64(root.value(QStringLiteral("docks")).toString().toLatin1());
    if (!docks.isEmpty())
    {
        restoreState(docks);
        // 旧布局常把日志区拉到 150+，空白挤占画布；恢复后统一收到 96。
        if (auto *logDock = findChild<QDockWidget *>(QStringLiteral("dock.log")))
            resizeDocks({logDock}, {96}, Qt::Vertical);
    }

    if (m_activeSource >= 0)
        switchSource(m_activeSource);

    m_timeMarkers.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("markers")).toArray())
    {
        const QJsonObject mo = v.toObject();
        m_timeMarkers.append({mo.value(QStringLiteral("t")).toDouble(),
                              mo.value(QStringLiteral("label")).toString()});
    }
    applyTimeMarkers();

    const int cur = root.value(QStringLiteral("current")).toInt(0);
    if (cur >= 0 && cur < m_pages->count() - 1)
        m_pages->setCurrentIndex(cur);

    const QJsonObject script = root.value(QStringLiteral("script")).toObject();
    if (!script.isEmpty())
    {
        if (m_script)
            m_script->setPlainText(script.value(QStringLiteral("source")).toString());
        const QJsonArray binds = script.value(QStringLiteral("inputs")).toArray();
        for (int i = 0; i < 4; ++i)
        {
            QComboBox *cb = m_scriptInputs[i];
            if (!cb)
                continue;
            cb->clear();
            cb->addItem(QStringLiteral("（未绑定）"), QString());
            const QString name = binds.at(i).toString();
            if (!name.isEmpty())
                cb->addItem(name, name);
            cb->setCurrentIndex(name.isEmpty() ? 0 : 1);
        }
        const QJsonObject paramObj = script.value(QStringLiteral("params")).toObject();
        if (m_scriptParams)
        {
            m_scriptParams->setRowCount(0);
            for (auto it = paramObj.begin(); it != paramObj.end(); ++it)
            {
                const int row = m_scriptParams->rowCount();
                m_scriptParams->insertRow(row);
                m_scriptParams->setItem(row, 0, new QTableWidgetItem(it.key()));
                m_scriptParams->setItem(row, 1, new QTableWidgetItem(
                    QString::number(it.value().toDouble(), 'g', 10)));
            }
        }
        if (!m_script->toPlainText().trimmed().isEmpty())
            runScript(false);
    }

    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已导入布局：%1").arg(path));
}

void MainWindow::openSettings()
{
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("设置"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *restore = new QCheckBox(QStringLiteral("启动时恢复上次布局"), &dlg);
    restore->setChecked(st.value(QStringLiteral("restoreLayout"), true).toBool());
    auto *grid = new QCheckBox(QStringLiteral("新建画布默认显示网格"), &dlg);
    grid->setChecked(st.value(QStringLiteral("gridDefault"), true).toBool());
    auto *legend = new QCheckBox(QStringLiteral("新建画布默认显示图例"), &dlg);
    legend->setChecked(st.value(QStringLiteral("legendDefault"), true).toBool());
    auto *timeFmt = new QComboBox(&dlg);
    timeFmt->addItem(QStringLiteral("相对秒"), QStringLiteral("relative"));
    timeFmt->addItem(QStringLiteral("时分秒"), QStringLiteral("clock"));
    timeFmt->addItem(QStringLiteral("UTC（Unix 秒时间戳）"), QStringLiteral("utc"));
    const QString savedFmt = st.value(QStringLiteral("timeFormat"), QStringLiteral("relative")).toString();
    timeFmt->setCurrentIndex(qMax(0, timeFmt->findData(savedFmt)));
    auto *exportCap = new QSpinBox(&dlg);
    exportCap->setRange(0, 100000000);
    exportCap->setSingleStep(1000);
    exportCap->setSuffix(QStringLiteral(" 点/通道（0=不限）"));
    exportCap->setValue(st.value(QStringLiteral("exportSampleLimit"), 0).toInt());
    auto *derivedCap = new QSpinBox(&dlg);
    derivedCap->setRange(0, 100000000);
    derivedCap->setSingleStep(100000);
    derivedCap->setSuffix(QStringLiteral(" 点（0=不限）"));
    derivedCap->setValue(st.value(QStringLiteral("derivedMaxPoints"), 2000000).toInt());
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("时间轴格式"), timeFmt);
    form->addRow(QStringLiteral("CSV 导出采样上限"), exportCap);
    form->addRow(QStringLiteral("派生通道重算上限"), derivedCap);
    lay->addWidget(restore);
    lay->addWidget(grid);
    lay->addWidget(legend);
    lay->addLayout(form);
    auto *clearCache = new QPushButton(QStringLiteral("清空解析缓存"), &dlg);
    lay->addWidget(clearCache);
    connect(clearCache, &QPushButton::clicked, this, [this] {
        DatasetCache::clear();
        log(QStringLiteral("解析缓存已清空"));
    });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    st.setValue(QStringLiteral("restoreLayout"), restore->isChecked());
    st.setValue(QStringLiteral("gridDefault"), grid->isChecked());
    st.setValue(QStringLiteral("legendDefault"), legend->isChecked());
    st.setValue(QStringLiteral("timeFormat"), timeFmt->currentData().toString());
    st.setValue(QStringLiteral("exportSampleLimit"), exportCap->value());
    st.setValue(QStringLiteral("derivedMaxPoints"), derivedCap->value());
    for (auto *c : findChildren<CanvasWidget *>())
        c->reloadDisplaySettings();
    log(QStringLiteral("设置已保存"));
}

QWidget *MainWindow::pageForCanvas(CanvasWidget *c) const
{
    if (!m_pages || !c)
        return nullptr;
    for (int i = 0; i < m_pages->count(); ++i)
    {
        QWidget *w = m_pages->widget(i);
        if (w && (w == c || w->isAncestorOf(c)))
            return w;
    }
    return nullptr;
}

QVector<CanvasWidget *> MainWindow::pageCanvases() const
{
    QVector<CanvasWidget *> out;
    if (m_pages)
        collectPageCanvases(m_pages->currentWidget(), out);
    return out;
}

CanvasWidget *MainWindow::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}



bool MainWindow::loadFile(const QString &path, bool interactive)
{
    return m_dataSourceDock->loadFile(path, interactive);
}

void MainWindow::connectLiveSource(const QString &extensionId)
{
    m_dataSourceDock->connectLiveSource(extensionId);
}

bool MainWindow::openLiveInput(const QStringList &ids, const QString &config, SourceData &dst,
                               QString *fanoutNote, QString *err)
{
    return m_dataSourceDock->openLiveInput(ids, config, dst, fanoutNote, err);
}

void MainWindow::configureLiveSource(SourceData *src) { m_dataSourceDock->configureLiveSource(src); }
void MainWindow::pollLiveSources() { m_dataSourceDock->pollLiveSources(); }
void MainWindow::refreshLiveSource(SourceData *src) { m_dataSourceDock->refreshLiveSource(src); }
void MainWindow::closeLiveSource(SourceData *src) { m_dataSourceDock->closeLiveSource(src); }



static QString plotModeLabel(int mode)
{
    static const char *names[] = {"时间序列 (TimeSeries)", "XY (Xy)", "散点 (Scatter)",
                                  "柱状 (BarChart)", "直方图 (Histogram)", "频谱 (Spectrum)",
                                  "地平仪 (Horizon)", "罗盘 (Compass)", "表盘 (Gauge)",
                                  "3D 轨迹 (Traj3D)", "GPS 地图 (GpsMap)", "3D 曲面 (Surface3D)"};
    if (mode < 0 || mode >= int(sizeof(names) / sizeof(names[0])))
        return QStringLiteral("未知");
    return QString::fromUtf8(names[mode]);
}

void MainWindow::refreshPropTree(CanvasWidget *active)
{
    if (!m_propTree)
        return;
    QWidget *page = active ? pageForCanvas(active)
                           : (m_pages ? m_pages->currentWidget() : nullptr);
    if (!page || !m_pages || page == m_pages->widget(m_pages->count() - 1))
        return;

    m_propTreeBuilding = true;
    m_propTree->clear();

    const QString pageTitle = m_pages->tabText(m_pages->indexOf(page));
    auto *root = new QTreeWidgetItem(m_propTree);
    root->setText(0, pageTitle);
    root->setIcon(0, QIcon(QStringLiteral(":/icons/panel.svg")));
    root->setFlags(root->flags() & ~Qt::ItemIsSelectable);
    root->setExpanded(true);

    QVector<CanvasWidget *> canvases;
    collectPageCanvases(page, canvases);
    QTreeWidgetItem *selected = nullptr;
    for (CanvasWidget *c : canvases)
    {
        auto *axes = new QTreeWidgetItem(root);
        axes->setText(0, c->title());
        axes->setIcon(0, QIcon(QStringLiteral(":/icons/prop.svg")));
        axes->setData(0, Qt::UserRole, 1);
        axes->setData(0, Qt::UserRole + 1, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(c)));
        axes->setExpanded(active ? (c == active) : (c == canvases.first()));

        const QJsonObject st = c->saveState();
        const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            const QJsonObject so = arr.at(i).toObject();
            const QString label = so.value(QStringLiteral("label")).toString(
                so.value(QStringLiteral("name")).toString());
            const int axis = so.value(QStringLiteral("axis")).toInt(0);
            auto *line = new QTreeWidgetItem(axes);
            line->setText(0, QStringLiteral("%1%2").arg(label, axis == 1 ? QStringLiteral("  → 右轴") : QString()));
            line->setData(0, Qt::UserRole, 2);
            line->setData(0, Qt::UserRole + 1, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(c)));
            line->setData(0, Qt::UserRole + 2, i);
            QPixmap sw(10, 10);
            sw.fill(QColor(so.value(QStringLiteral("color")).toString()));
            line->setIcon(0, QIcon(sw));
        }
        if (c == active || (!active && !selected))
            selected = axes;
    }
    m_propTreeBuilding = false;

    if (selected)
    {
        m_propTree->setCurrentItem(selected);
        CanvasWidget *c = reinterpret_cast<CanvasWidget *>(
            selected->data(0, Qt::UserRole + 1).value<quintptr>());
        if (c)
            showAxesProps(c);
    }
}

void MainWindow::showAxesProps(CanvasWidget *c)
{
    if (!c)
        return;
    const QJsonObject st = c->saveState();
    const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
    int right = 0;
    for (const QJsonValue &v : arr)
        if (v.toObject().value(QStringLiteral("axis")).toInt(0) == 1)
            ++right;
    const bool autoY = st.value(QStringLiteral("autoY")).toBool(true);
    const QString y1 = autoY ? QStringLiteral("自动量程")
                             : QStringLiteral("%1 ~ %2").arg(st.value(QStringLiteral("yMin")).toDouble(), 0, 'f', 3)
                                                       .arg(st.value(QStringLiteral("yMax")).toDouble(), 0, 'f', 3);
    const QString y2 = right > 0
        ? QStringLiteral("%1 ~ %2").arg(st.value(QStringLiteral("y2Min")).toDouble(), 0, 'f', 3)
                                    .arg(st.value(QStringLiteral("y2Max")).toDouble(), 0, 'f', 3)
        : QStringLiteral("未启用");
    setPropRows(QStringLiteral("坐标轴 · %1").arg(c->title()),
                {{QStringLiteral("类型"), QStringLiteral("坐标轴 (Axes)")},
                 {QStringLiteral("绘图模式"), plotModeLabel(st.value(QStringLiteral("mode")).toInt())},
                 {QStringLiteral("时间轴"), QStringLiteral("%1 ~ %2 s")
                                               .arg(st.value(QStringLiteral("t0")).toDouble(), 0, 'f', 2)
                                               .arg(st.value(QStringLiteral("t1")).toDouble(), 0, 'f', 2)},
                 {QStringLiteral("X 轴标签"), st.value(QStringLiteral("xlabel")).toString().isEmpty()
                                                   ? QStringLiteral("—")
                                                   : st.value(QStringLiteral("xlabel")).toString()},
                 {QStringLiteral("Y 轴标签"), st.value(QStringLiteral("ylabel")).toString().isEmpty()
                                                   ? QStringLiteral("—")
                                                   : st.value(QStringLiteral("ylabel")).toString()},
                 {QStringLiteral("左 Y 轴"), y1},
                 {QStringLiteral("右 Y 轴"), y2},
                 {QStringLiteral("曲线"), QStringLiteral("%1 条（右轴 %2）").arg(arr.size()).arg(right)},
                 {QStringLiteral("X 网格"), st.value(QStringLiteral("gridX")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("Y 网格"), st.value(QStringLiteral("gridY")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("次网格"), st.value(QStringLiteral("minorGrid")).toBool(false) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("图例"), st.value(QStringLiteral("legend")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("图例位置"), QStringList{QStringLiteral("左上"), QStringLiteral("右上"),
                                                          QStringLiteral("左下"), QStringLiteral("右下")}
                                                     .value(st.value(QStringLiteral("legendPos")).toInt(0))},
                 {QStringLiteral("边框"), st.value(QStringLiteral("box")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("FlipY"), st.value(QStringLiteral("flipY")).toBool(false) ? QStringLiteral("是") : QStringLiteral("否")},
                 {QStringLiteral("坐标轴字号"), QStringLiteral("%1 px").arg(st.value(QStringLiteral("axisFontSize")).toInt(11))},
                 {QStringLiteral("坐标轴线宽"), QStringLiteral("%1 px").arg(st.value(QStringLiteral("axisLineWidth")).toInt(1))},
                 {QStringLiteral("坐标轴颜色"), st.value(QStringLiteral("axisColor")).toString().isEmpty()
                                                   ? QStringLiteral("跟随主题")
                                                   : st.value(QStringLiteral("axisColor")).toString()}});
    m_propCanvas = c;
    m_propSeriesIndex = -1;
    m_propKind = 1;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(true);
}

void MainWindow::editAxesProps(CanvasWidget *c)
{
    if (!c)
        return;
    AxesEditDialog dlg(c->saveState(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    c->setTitle(dlg.title());
    c->setAxisLabels(dlg.xLabel(), dlg.yLabel());
    c->setGridAxes(dlg.gridX(), dlg.gridY(), dlg.minorGrid());
    c->setBoxVisible(dlg.box());
    c->setLegendVisible(dlg.legend());
    c->setLegendPosition(dlg.legendPosition());
    c->setFlipY(dlg.flipY());
    c->setAxisFontSize(dlg.fontSize());
    c->setAxisLineWidth(dlg.lineWidth());
    c->setAxisColor(dlg.axisColorValue());
    showAxesProps(c);
}

void MainWindow::showSeriesProps(CanvasWidget *c, int index)
{
    if (!c)
        return;
    const QJsonObject st = c->saveState();
    const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
    if (index < 0 || index >= arr.size())
        return;
    const QJsonObject so = arr.at(index).toObject();
    setPropRows(QStringLiteral("曲线 · %1").arg(so.value(QStringLiteral("label")).toString(
                                               so.value(QStringLiteral("name")).toString())),
                {{QStringLiteral("类型"), QStringLiteral("曲线 (Line)")},
                 {QStringLiteral("名称"), so.value(QStringLiteral("name")).toString()},
                 {QStringLiteral("图例"), so.value(QStringLiteral("label")).toString()},
                 {QStringLiteral("单位"), so.value(QStringLiteral("unit")).toString().isEmpty()
                                               ? QStringLiteral("—")
                                               : so.value(QStringLiteral("unit")).toString()},
                 {QStringLiteral("颜色"), so.value(QStringLiteral("color")).toString()},
                 {QStringLiteral("线宽"), QStringLiteral("%1 px").arg(so.value(QStringLiteral("width")).toInt(1))},
                 {QStringLiteral("线型"), QStringList{QStringLiteral("实线"), QStringLiteral("虚线"),
                                                    QStringLiteral("点线"), QStringLiteral("点划线")}
                                               .value(so.value(QStringLiteral("style")).toInt(0))},
                 {QStringLiteral("标记"), QStringList{QStringLiteral("无"), QStringLiteral("圆点"), QStringLiteral("方块"),
                                                        QStringLiteral("菱形"), QStringLiteral("三角"), QStringLiteral("十字"),
                                                        QStringLiteral("叉号")}
                                                   .value(so.value(QStringLiteral("marker")).toInt(0))},
                 {QStringLiteral("标记大小"), QStringLiteral("%1 px").arg(so.value(QStringLiteral("markerSize")).toInt(5))},
                 {QStringLiteral("绑定轴"), so.value(QStringLiteral("axis")).toInt(0) == 1 ? QStringLiteral("右 Y 轴")
                                                                                          : QStringLiteral("左 Y 轴")},
                 {QStringLiteral("可见"), so.value(QStringLiteral("visible")).toBool(true) ? QStringLiteral("是")
                                                                                         : QStringLiteral("否")},
                 {QStringLiteral("点数"), QString::number(so.value(QStringLiteral("points")).toInt())},
                 {QStringLiteral("时间偏移"), QStringLiteral("%1 s").arg(so.value(QStringLiteral("offset")).toDouble(), 0, 'f', 3)},
                 {QStringLiteral("来源"), so.value(QStringLiteral("srcPath")).toString().isEmpty()
                                               ? QStringLiteral("演示 / 脚本生成")
                                               : so.value(QStringLiteral("srcPath")).toString()}});
    m_propCanvas = c;
    m_propSeriesIndex = index;
    m_propKind = 2;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(true);
}

void MainWindow::editSeriesProps(CanvasWidget *c, int index)
{
    if (!c || index < 0)
        return;
    const QJsonObject st = c->seriesState(index);
    if (st.isEmpty())
        return;

    ps::data_source::SourceData *src = nullptr;
    const int sid = st.value(QStringLiteral("srcId")).toInt(-1);
    const QString path = st.value(QStringLiteral("srcPath")).toString();
    for (ps::data_source::SourceData &sd : m_sourceStore.items())
        if (sd.id == sid || (!path.isEmpty() && sd.path == path))
        {
            src = &sd;
            break;
        }
    QVector<const PsChannelInfo *> channels;
    QString alias;
    if (src)
    {
        channels = src->channels;
        alias = src->alias.isEmpty() ? src->name : src->alias;
    }

    SeriesEditDialog dlg(st, channels, alias, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonObject patch = dlg.patch();
    const int op = dlg.transformId();
    const PsChannelInfo *ch = dlg.channel();
    const double off = patch.value(QStringLiteral("offset")).toDouble();
    const QString oldName = st.value(QStringLiteral("name")).toString();

    if (op != 0 && ch)
    {
        const quint32 n = ch->point_count;
        if (n < 2 || !ch->time || !ch->value)
            return;
        QVector<double> tt(n), vv(n);
        for (quint32 i = 0; i < n; ++i)
        {
            tt[i] = ch->time[i] - off;
            vv[i] = ch->value[i];
        }
        QString suffix;
        switch (op)
        {
        case 1:
            suffix = QStringLiteral("微分");
            for (quint32 i = n - 1; i > 0; --i)
            {
                const double dt = ch->time[i] - ch->time[i - 1];
                vv[i] = dt > 1e-12 ? (ch->value[i] - ch->value[i - 1]) / dt : 0.0;
            }
            vv[0] = 0.0;
            break;
        case 2:
        {
            suffix = QStringLiteral("积分");
            double acc = 0.0;
            vv[0] = 0.0;
            for (quint32 i = 1; i < n; ++i)
            {
                const double dt = ch->time[i] - ch->time[i - 1];
                acc += (ch->value[i] + ch->value[i - 1]) * 0.5 * dt;
                vv[i] = acc;
            }
            break;
        }
        case 3:
        {
            suffix = QStringLiteral("滑动平均");
            const int w = dlg.windowSize();
            QVector<double> out(n);
            double sum = 0.0;
            for (quint32 i = 0; i < n; ++i)
            {
                sum += vv[i];
                if (int(i) >= w)
                    sum -= vv[i - w];
                out[i] = sum / qMin<int>(int(i) + 1, w);
            }
            vv = out;
            break;
        }
        case 4:
            suffix = QStringLiteral("绝对值");
            for (quint32 i = 0; i < n; ++i)
                vv[i] = std::fabs(vv[i]);
            break;
        case 5:
        {
            suffix = QStringLiteral("归一化");
            double lo = 1e300, hi = -1e300;
            for (double v : vv)
            {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const double span = hi - lo;
            for (quint32 i = 0; i < n; ++i)
                vv[i] = span > 1e-12 ? (vv[i] - lo) / span : 0.0;
            break;
        }
        case 6:
        {
            suffix = QStringLiteral("脚本");
            ScriptSeries in;
            in.name = dlg.channelName();
            in.time = tt;
            in.value = vv;
            const ScriptResult r = m_extensions.registry().runScript(dlg.scriptText(), {in});
            if (!r.ok || r.outputs.isEmpty())
            {
                QMessageBox::warning(this, QStringLiteral("脚本错误"), r.error.isEmpty()
                    ? QStringLiteral("脚本没有输出") : r.error);
                return;
            }
            const ScriptOutput &out = r.outputs.first();
            QString label = patch.value(QStringLiteral("label")).toString();
            if (!label.endsWith(QStringLiteral("· 脚本")))
                label += QStringLiteral(" · 脚本");
            patch[QStringLiteral("label")] = label;
            patch[QStringLiteral("offset")] = 0.0;
            c->setSeriesSamples(index, out.time, out.value, label);
            c->patchSeries(index, patch);
            showSeriesProps(c, index);
            log(QStringLiteral("曲线脚本变换：%1").arg(label));
            return;
        }
        }
        QString label = patch.value(QStringLiteral("label")).toString();
        if (!label.endsWith(QStringLiteral("· ") + suffix))
            label += QStringLiteral(" · ") + suffix;
        patch[QStringLiteral("label")] = label;
        patch[QStringLiteral("offset")] = 0.0;
        c->setSeriesSamples(index, tt, vv, label);
        c->patchSeries(index, patch);
        showSeriesProps(c, index);
        log(QStringLiteral("曲线变换：%1").arg(label));
        return;
    }

    if (ch && dlg.channelName() != oldName)
        c->setSeriesChannel(index, ch, sid, src ? src->path : path,
                            src ? (src->alias.isEmpty() ? src->name : src->alias) : alias, off);
    c->patchSeries(index, patch);
    showSeriesProps(c, index);
}

void MainWindow::setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows)
{
    m_propCanvas = nullptr;
    m_propSeriesIndex = -1;
    m_propKind = 0;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(false);
    m_selLabel->setText(QStringLiteral("当前对象：%1").arg(object));
    m_propTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        auto *k = new QTableWidgetItem(rows[i].first);
        k->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        auto *v = new QTableWidgetItem(rows[i].second);
        // 属性表当前只做只读展示，避免看起来能改、改完却不生效。
        v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        v->setToolTip(rows[i].second);
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_propTable->setItem(i, 0, k);
        m_propTable->setItem(i, 1, v);
    }
}

void MainWindow::log(const QString &text)
{
    if (!m_logView)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
    statusBar()->showMessage(text, 4000);
}
