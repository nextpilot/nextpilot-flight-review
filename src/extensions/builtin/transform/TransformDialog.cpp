#include "TransformDialog.h"
#include "SettingsManager.h"

#include "ps_kernel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
struct FilterDef
{
    const char *id;
    const char *label;
};

uint32_t channelPoints(PsDataset *ds, const QString &name)
{
    const uint32_t n = ps_dataset_channel_count(ds);
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (ch && name == QString::fromUtf8(ch->name))
            return ch->point_count;
    }
    return 0;
}

const FilterDef kFilters[] = {
    {"none", "直通（无变化）"},
    {"absolute", "绝对值 |y|"},
    {"scale", "比例 + 偏移 + 时间平移"},
    {"derivative", "微分 dv/dt"},
    {"integral", "梯形积分"},
    {"moving_average", "滑动平均"},
    {"moving_rms", "滑动 RMS"},
    {"moving_variance", "滑动方差 / 标准差"},
    {"outlier_removal", "剔除单点尖峰"},
    {"samples_counter", "窗口内样本数"},
    {"binary_filter", "阈值二值化"},
    {"time_since_previous", "相邻样本 Δt"},
};

QDoubleSpinBox *dspin(double lo, double hi, double val, int decimals = 4)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setValue(val);
    s->setSingleStep(1.0);
    return s;
}

QSpinBox *ispin(int lo, int hi, int val)
{
    auto *s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(val);
    return s;
}

} // namespace

class PreviewPlot : public QWidget
{
public:
    explicit PreviewPlot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(112);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setData(QVector<double> t, QVector<double> in, QVector<double> out)
    {
        m_t = std::move(t);
        m_in = std::move(in);
        m_out = std::move(out);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(QStringLiteral("#0d1117")));
        p.setPen(QColor(QStringLiteral("#232b33")));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRectF area = rect().adjusted(10, 22, -12, -12);
        if (m_in.size() < 2 || m_out.size() < 2)
        {
            p.setPen(QColor(QStringLiteral("#5d6c77")));
            p.drawText(area, Qt::AlignCenter, QStringLiteral("无预览数据"));
            return;
        }

        double lo = m_in.first(), hi = m_in.first();
        for (double v : m_in) { lo = std::min(lo, v); hi = std::max(hi, v); }
        for (double v : m_out) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (hi - lo < 1e-12) { hi += 1.0; lo -= 1.0; }

        auto xy = [&](double t, double v) {
            const double x = area.left() + (t - m_t.first()) / std::max(1e-12, m_t.last() - m_t.first()) * area.width();
            const double y = area.bottom() - (v - lo) / (hi - lo) * area.height();
            return QPointF(x, y);
        };

        p.setPen(QPen(QColor(QStringLiteral("#232b33")), 1));
        for (int i = 1; i < 4; ++i)
        {
            const double y = area.top() + area.height() * i / 4.0;
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        auto stroke = [&](const QVector<double> &v, const QPen &pen) {
            QPainterPath path;
            const int step = std::max(1, int(v.size()) / std::max(1, width() * 2));
            bool first = true;
            for (int i = 0; i < v.size(); i += step)
            {
                const int ti = std::min(i, int(m_t.size()) - 1);
                const QPointF pt = xy(m_t.at(ti), v.at(i));
                if (first) { path.moveTo(pt); first = false; }
                else path.lineTo(pt);
            }
            p.setPen(pen);
            p.drawPath(path);
        };
        stroke(m_in, QPen(QColor(QStringLiteral("#5d6c77")), 1, Qt::DashLine));
        stroke(m_out, QPen(QColor(QStringLiteral("#f2b134")), 1.6));

        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(QColor(QStringLiteral("#5d6c77")));
        p.drawText(QRectF(10, 4, width() / 2, 16), Qt::AlignLeft, QStringLiteral("— 原始"));
        p.setPen(QColor(QStringLiteral("#f2b134")));
        p.drawText(QRectF(width() / 2.0, 4, width() / 2.0 - 10, 16), Qt::AlignRight, QStringLiteral("— 结果"));
    }

private:
    QVector<double> m_t, m_in, m_out;
};


TransformDialogResult TransformDialog::transformResult() const
{
    TransformDialogResult r;
    r.addedNames = m_added;
    r.inputName = m_inputName;
    r.kind = m_kind;
    r.filterId = m_filterId;
    r.params = m_params;
    r.outputName = m_outputName;
    r.quat[0] = m_quat[0];
    r.quat[1] = m_quat[1];
    r.quat[2] = m_quat[2];
    r.quat[3] = m_quat[3];
    r.degrees = m_degreesOn;
    r.unwrap = m_unwrapOn;
    return r;
}

TransformDialog::TransformDialog(PsDataset *ds, const QVector<const PsChannelInfo *> &channels,
                                 const QString &sourceName, const QString &preselect, QWidget *parent)
    : QDialog(parent)
    , m_ds(ds)
    , m_channels(channels)
    , m_preselect(preselect)
{
    setWindowTitle(QStringLiteral("添加变换 · %1").arg(sourceName));
    setMinimumWidth(460);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#161b20;color:#c9d1d9;}"
        "QLabel{color:#c9d1d9;background:transparent;}"
        "QTabWidget::pane{border:1px solid #2d3841;top:-1px;}"
        "QTabBar::tab{background:#1a2127;color:#8494a0;padding:6px 14px;border:1px solid #2d3841;border-bottom:0;}"
        "QTabBar::tab:selected{background:#0d1117;color:#f2b134;}"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#0d1117;color:#c9d1d9;"
        "border:1px solid #2d3841;border-radius:2px;padding:4px 6px;min-height:22px;}"
        "QComboBox QAbstractItemView{background:#161b20;color:#c9d1d9;selection-background-color:#2d3841;}"
        "QCheckBox{color:#c9d1d9;background:transparent;}"
        "QPushButton{background:#232b33;color:#c9d1d9;border:1px solid #2d3841;padding:5px 16px;border-radius:2px;}"
        "QPushButton:hover{background:#2d3841;color:#f2b134;}"));

    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    tabs->addTab(buildFilterPage(), QStringLiteral("单通道滤波"));
    tabs->addTab(buildQuatPage(), QStringLiteral("四元数 → RPY"));
    root->addWidget(tabs, 1);

    auto *box = new QDialogButtonBox;
    auto *cancel = box->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    auto *apply = box->addButton(QStringLiteral("应用"), QDialogButtonBox::AcceptRole);
    root->addWidget(box);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, &TransformDialog::apply);
}

QWidget *TransformDialog::buildFilterPage()
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->setContentsMargins(16, 14, 16, 12);
    form->setSpacing(8);

    m_input = new QComboBox;
    for (const PsChannelInfo *ch : m_channels)
        if (ch->point_count > 1 && ch->time && ch->value)
            m_input->addItem(QString::fromUtf8(ch->name));
    form->addRow(QStringLiteral("输入通道"), m_input);

    m_filter = new QComboBox;
    for (const auto &f : kFilters)
        m_filter->addItem(QString::fromUtf8(f.label), QString::fromUtf8(f.id));
    form->addRow(QStringLiteral("算子"), m_filter);

    m_paramStack = new QStackedWidget;
    m_paramStack->setFixedHeight(118);
    for (const auto &f : kFilters)
        m_paramStack->addWidget(buildParamsPage(QString::fromUtf8(f.id)));
    form->addRow(QStringLiteral("参数"), m_paramStack);

    m_output = new QLineEdit;
    form->addRow(QStringLiteral("输出通道"), m_output);

    m_preview = new PreviewPlot;
    form->addRow(QStringLiteral("预览"), m_preview);

    connect(m_filter, &QComboBox::currentIndexChanged, this, &TransformDialog::onFilterChanged);
    connect(m_input, &QComboBox::currentTextChanged, this, &TransformDialog::refreshOutputName);
    connect(m_output, &QLineEdit::textEdited, this, [this] { m_autoOutput = false; });

    const int pre = m_input->findText(m_preselect);
    m_input->setCurrentIndex(pre >= 0 ? pre : (m_input->count() > 0 ? 0 : -1));
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(120);
    connect(m_previewTimer, &QTimer::timeout, this, &TransformDialog::updatePreview);
    for (QDoubleSpinBox *b : std::as_const(m_doubles))
        connect(b, &QDoubleSpinBox::valueChanged, m_previewTimer, qOverload<>(&QTimer::start));
    for (QSpinBox *b : std::as_const(m_ints))
        connect(b, &QSpinBox::valueChanged, m_previewTimer, qOverload<>(&QTimer::start));
    for (QCheckBox *b : std::as_const(m_checks))
        connect(b, &QCheckBox::toggled, m_previewTimer, qOverload<>(&QTimer::start));
    for (QComboBox *b : std::as_const(m_combos))
        connect(b, &QComboBox::currentIndexChanged, m_previewTimer, qOverload<>(&QTimer::start));
    connect(m_input, &QComboBox::currentTextChanged, m_previewTimer, qOverload<>(&QTimer::start));
    connect(m_filter, &QComboBox::currentIndexChanged, m_previewTimer, qOverload<>(&QTimer::start));

    onFilterChanged(0);
    updatePreview();
    return w;
}

QWidget *TransformDialog::buildParamsPage(const QString &id)
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);
    auto addD = [&](const QString &key, const QString &label, double lo, double hi, double val, int dec = 4) {
        auto *s = dspin(lo, hi, val, dec);
        m_doubles.insert(id + QLatin1Char('.') + key, s);
        form->addRow(label, s);
    };
    auto addI = [&](const QString &key, const QString &label, int lo, int hi, int val) {
        auto *s = ispin(lo, hi, val);
        m_ints.insert(id + QLatin1Char('.') + key, s);
        form->addRow(label, s);
    };
    auto addB = [&](const QString &key, const QString &label, bool val) {
        auto *s = new QCheckBox(label);
        s->setChecked(val);
        m_checks.insert(id + QLatin1Char('.') + key, s);
        form->addRow(QString(), s);
    };

    if (id == QLatin1String("scale"))
    {
        addD("value_scale", QStringLiteral("比例"), -1e12, 1e12, 1.0, 6);
        addD("value_offset", QStringLiteral("偏移"), -1e12, 1e12, 0.0, 6);
        addD("time_offset_sec", QStringLiteral("时间平移 (s)"), -1e9, 1e9, 0.0, 6);
    }
    else if (id == QLatin1String("derivative") || id == QLatin1String("integral"))
    {
        addB("use_custom_dt", QStringLiteral("固定 Δt"), false);
        addD("custom_dt", QStringLiteral("Δt (s)"), 1e-9, 1e9, 1.0, 6);
    }
    else if (id == QLatin1String("moving_average"))
    {
        addI("window", QStringLiteral("窗口"), 1, 1000000, 100);
        addB("compensate_time_offset", QStringLiteral("时间戳居中"), false);
    }
    else if (id == QLatin1String("moving_rms"))
        addI("window", QStringLiteral("窗口"), 1, 1000000, 100);
    else if (id == QLatin1String("moving_variance"))
    {
        addI("window", QStringLiteral("窗口"), 1, 1000000, 100);
        addB("std_dev", QStringLiteral("输出标准差"), false);
    }
    else if (id == QLatin1String("outlier_removal"))
        addD("outlier_factor", QStringLiteral("倍数阈值"), 0.0, 1e12, 100.0, 3);
    else if (id == QLatin1String("samples_counter"))
        addD("samples_ms", QStringLiteral("窗口 (ms)"), 0.0, 1e9, 1000.0, 3);
    else if (id == QLatin1String("binary_filter"))
    {
        auto *op = new QComboBox;
        op->addItem(QStringLiteral("== a"), 0);
        op->addItem(QStringLiteral("< a"), 1);
        op->addItem(QStringLiteral("<= a"), 2);
        op->addItem(QStringLiteral("> a"), 3);
        op->addItem(QStringLiteral(">= a"), 4);
        op->addItem(QStringLiteral("in [a,b]"), 5);
        op->setCurrentIndex(3);
        m_combos.insert(id + QStringLiteral(".binary_op"), op);
        form->addRow(QStringLiteral("比较"), op);
        addD("binary_a", QStringLiteral("阈值 a"), -1e12, 1e12, 0.0, 6);
        addD("binary_b", QStringLiteral("阈值 b"), -1e12, 1e12, 0.0, 6);
        addD("binary_tol", QStringLiteral("== 容差"), 0.0, 1e9, 1e-9, 9);
    }
    else
    {
        auto *tip = new QLabel(QStringLiteral("该算子无参数"));
        tip->setStyleSheet(QStringLiteral("color:#8494a0;"));
        form->addRow(tip);
    }
    return w;
}

QWidget *TransformDialog::buildQuatPage()
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->setContentsMargins(16, 14, 16, 12);
    form->setSpacing(8);

    QComboBox *combos[4] = {nullptr, nullptr, nullptr, nullptr};
    const QStringList labels = {QStringLiteral("w"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
    for (int i = 0; i < 4; ++i)
    {
        auto *c = new QComboBox;
        for (const PsChannelInfo *ch : m_channels)
            if (ch->point_count > 1)
                c->addItem(QString::fromUtf8(ch->name));
        combos[i] = c;
        form->addRow(labels[i], c);
    }
    m_qw = combos[0];
    m_qx = combos[1];
    m_qy = combos[2];
    m_qz = combos[3];

    // 尽量按 q0..q3 预选；找不到就按顺序取前四路
    const QStringList suffixes = {QStringLiteral("q0"), QStringLiteral("q1"), QStringLiteral("q2"), QStringLiteral("q3")};
    QComboBox *all[4] = {m_qw, m_qx, m_qy, m_qz};
    const QStringList words = {QStringLiteral("w"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
    for (int i = 0; i < 4; ++i)
    {
        int idx = -1;
        for (int k = 0; k < all[i]->count() && idx < 0; ++k)
            if (all[i]->itemText(k).endsWith(QLatin1Char('.') + suffixes[i], Qt::CaseInsensitive))
                idx = k;
        for (int k = 0; k < all[i]->count() && idx < 0; ++k)
            if (all[i]->itemText(k).compare(words[i], Qt::CaseInsensitive) == 0
                || all[i]->itemText(k).endsWith(QLatin1Char('.') + words[i], Qt::CaseInsensitive))
                idx = k;
        all[i]->setCurrentIndex(idx >= 0 ? idx : qMin(i, all[i]->count() - 1));
    }

    m_qout = new QLineEdit(QStringLiteral("rpy"));
    form->addRow(QStringLiteral("输出主题"), m_qout);
    m_degrees = new QCheckBox(QStringLiteral("角度制（默认弧度）"));
    m_degrees->setChecked(true);
    form->addRow(QString(), m_degrees);
    m_unwrap = new QCheckBox(QStringLiteral("展开 ±π 跳变"));
    m_unwrap->setChecked(true);
    form->addRow(QString(), m_unwrap);
    return w;
}

void TransformDialog::onFilterChanged(int index)
{
    if (!m_filter || !m_paramStack)
        return;
    m_paramStack->setCurrentIndex(index);
    refreshOutputName();
}

void TransformDialog::refreshOutputName()
{
    if (!m_autoOutput || !m_output || !m_input || !m_filter)
        return;
    if (m_input->currentText().isEmpty())
        return;
    m_output->setText(m_input->currentText() + QLatin1Char('.') + m_filter->currentData().toString());
}

QJsonObject TransformDialog::currentParams(const QString &id) const
{
    QJsonObject p;
    const QString prefix = id + QLatin1Char('.');
    for (auto it = m_doubles.constBegin(); it != m_doubles.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            p.insert(it.key().mid(prefix.size()), it.value()->value());
    for (auto it = m_ints.constBegin(); it != m_ints.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            p.insert(it.key().mid(prefix.size()), it.value()->value());
    for (auto it = m_checks.constBegin(); it != m_checks.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            p.insert(it.key().mid(prefix.size()), it.value()->isChecked());
    for (auto it = m_combos.constBegin(); it != m_combos.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            p.insert(it.key().mid(prefix.size()), it.value()->currentData().toInt());
    return p;
}

void TransformDialog::updatePreview()
{
    if (!m_preview || !m_ds || !m_input || !m_filter)
        return;
    const QString name = m_input->currentText();
    const PsChannelInfo *ch = nullptr;
    for (const PsChannelInfo *c : m_channels)
        if (name == QString::fromUtf8(c->name)) { ch = c; break; }
    if (!ch || ch->point_count < 2)
    {
        m_preview->setData({}, {}, {});
        return;
    }
    const uint32_t n = ch->point_count;
    QVector<double> in_t(n), in_v(n), out_t(n), out_v(n);
    std::copy(ch->time, ch->time + n, in_t.begin());
    std::copy(ch->value, ch->value + n, in_v.begin());

    const QString id = m_filter->currentData().toString();
    const QByteArray json = QJsonDocument(currentParams(id)).toJson(QJsonDocument::Compact);
    char err[256] = {0};
    uint32_t got = 0;
    const int rc = ps_preview_filter(m_ds, name.toUtf8().constData(), id.toUtf8().constData(),
                                    json.constData(), out_t.data(), out_v.data(), n, &got, err, sizeof err);
    if (rc != 0 && rc != 6)
    {
        m_preview->setData({}, {}, {});
        return;
    }
    out_t.resize(got);
    out_v.resize(got);
    m_preview->setData(in_t, in_v, out_v);
}

void TransformDialog::apply()
{
    if (!m_ds)
        return;
    const int maxPoints = SettingsManager::instance().get(ps::settings::kDerivedMaxPoints);
    if (maxPoints > 0)
    {
        bool quatTab = false;
        if (auto *tabs = findChild<QTabWidget *>())
            quatTab = tabs->currentIndex() == 1;
        uint32_t worst = 0;
        QString worstName;
        QStringList ins;
        if (quatTab)
            ins = {m_qw->currentText(), m_qx->currentText(), m_qy->currentText(), m_qz->currentText()};
        else
            ins = {m_input->currentText()};
        for (const QString &name : ins)
        {
            const uint32_t n = channelPoints(m_ds, name);
            if (n > worst)
            {
                worst = n;
                worstName = name;
            }
        }
        if (worst > static_cast<uint32_t>(maxPoints))
        {
            QMessageBox::warning(this, QStringLiteral("派生通道"),
                                 QStringLiteral("输入 %1 有 %2 点，超过重算上限 %3；\n"
                                                "请在“设置”里调高派生通道重算上限。")
                                     .arg(worstName).arg(worst).arg(maxPoints));
            return;
        }
    }
    char err[512] = {0};
    if (auto *tabs = findChild<QTabWidget *>())
    {
        if (tabs->currentIndex() == 1)
        {
            const QString topic = m_qout->text().trimmed().isEmpty() ? QStringLiteral("rpy") : m_qout->text().trimmed();
            const int rc = ps_dataset_apply_quat_rpy(
                m_ds, m_qw->currentText().toUtf8().constData(), m_qx->currentText().toUtf8().constData(),
                m_qy->currentText().toUtf8().constData(), m_qz->currentText().toUtf8().constData(),
                topic.toUtf8().constData(), m_degrees->isChecked() ? 1 : 0,
                m_unwrap->isChecked() ? 1 : 0, err, sizeof err);
            if (rc != 0)
            {
                QMessageBox::warning(this, QStringLiteral("变换失败"), QString::fromUtf8(err));
                return;
            }
            m_kind = QStringLiteral("quat");
            m_quat[0] = m_qw->currentText();
            m_quat[1] = m_qx->currentText();
            m_quat[2] = m_qy->currentText();
            m_quat[3] = m_qz->currentText();
            m_outputName = topic;
            m_degreesOn = m_degrees->isChecked();
            m_unwrapOn = m_unwrap->isChecked();
            m_added = {topic + QStringLiteral(".roll"), topic + QStringLiteral(".pitch"),
                       topic + QStringLiteral(".yaw")};
            accept();
            return;
        }
    }

    const QString id = m_filter->currentData().toString();
    m_inputName = m_input->currentText();
    const QString out = m_output->text().trimmed();
    const QJsonObject p = currentParams(id);
    const QByteArray json = QJsonDocument(p).toJson(QJsonDocument::Compact);
    const int rc = ps_dataset_apply_filter(m_ds, m_input->currentText().toUtf8().constData(),
                                           id.toUtf8().constData(), json.constData(),
                                           out.toUtf8().constData(), err, sizeof err);
    if (rc != 0)
    {
        QMessageBox::warning(this, QStringLiteral("变换失败"), QString::fromUtf8(err));
        return;
    }
    m_kind = QStringLiteral("filter");
    m_filterId = id;
    m_outputName = out;
    m_params = p;
    m_added = {out};
    accept();
}
