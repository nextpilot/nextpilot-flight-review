#include "ProtocolEditorDialog.h"

#include "ThemeManager.h"
#include "TransformEditorDialog.h"
#include "AppContext.h"
#include "Transform.h"

#include "ProtocolIntentHandler.h"
#include "ProtocolTemplateStore.h"

#include "IIntentExecutor.h"
#include "Intent.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QShortcut>
#include <QSplitter>
#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
{

namespace
{
// 段表列：沿用老编辑器的「字段 / 长度 / 类型 / 取值」，末尾加一列"其它键"（只读摘要），
// 让 scale / bit / digest / enum 这些结构化键可见但不挤进可编辑格——具体值在"段详情"里改。
enum SegCol
{
    SegField = 0,
    SegType,    // 类型紧随字段名：改类型比改长度频繁
    SegCount,
    SegValue,
    SegUnit,
    SegOther,   // 只读摘要：表里没列的键（scale / bit / digest / enum / range / if …）
    SegColCount
};

QJsonObject objectFromItem(const QTableWidgetItem *item)
{
    if (!item)
        return {};
    const QJsonDocument d = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray());
    return d.isObject() ? d.object() : QJsonObject{};
}

void storeObjectOnItem(QTableWidgetItem *item, const QJsonObject &o)
{
    if (!item)
        return;
    item->setData(Qt::UserRole, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// 行顺序就是协议语义（段按顺序读），所以拖拽必须能真搬行。
// 老编辑器的 ReorderTable 原样搬：段表里所有单元格都是纯文本 item，没有 cellWidget，
// 因此整行 takeItem/insertRow 不会丢控件。
class ReorderTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    std::function<void(int, int)> onMove;

protected:
    void dropEvent(QDropEvent *e) override
    {
        int from = currentRow();
        if (from < 0 && !selectedItems().isEmpty())
            from = selectedItems().first()->row();
        int row = indexAt(e->position().toPoint()).row();
        if (row < 0)
            row = rowCount();
        if (dropIndicatorPosition() == QAbstractItemView::BelowItem)
            ++row;
        if (from < row)
            --row;
        row = std::max(0, std::min(row, rowCount() - 1));
        if (from >= 0 && from != row && onMove)
        {
            onMove(from, row);
            e->acceptProposedAction();
            return;
        }
        QTableWidget::dropEvent(e);
    }
};

void moveTableRow(QTableWidget *t, int from, int to)
{
    if (!t || from == to || from < 0 || from >= t->rowCount() || to < 0 || to >= t->rowCount())
        return;
    QList<QTableWidgetItem *> items;
    for (int c = 0; c < t->columnCount(); ++c)
        items.append(t->takeItem(from, c));
    t->removeRow(from);
    t->insertRow(to);
    for (int c = 0; c < t->columnCount(); ++c)
        t->setItem(to, c, items.value(c));
    t->setCurrentCell(to, 0);
}


// 内置类型 + 模板 types 别名；段表"类型"列用它做纯下拉，禁止手输。
const QStringList &baseTypeOptions()
{
    static const QStringList opts = {
        QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"),
        QStringLiteral("i16"), QStringLiteral("u32"), QStringLiteral("i32"),
        QStringLiteral("u64"), QStringLiteral("i64"), QStringLiteral("f32"),
        QStringLiteral("f64"), QStringLiteral("bool"), QStringLiteral("bcd"),
        QStringLiteral("char"), QStringLiteral("string"), QStringLiteral("number"),
        QStringLiteral("rest"), QStringLiteral("time")};
    return opts;
}

QStringList typeOptionsWith(const QJsonObject &types)
{
    QStringList opts = baseTypeOptions();
    for (auto it = types.begin(); it != types.end(); ++it)
        opts << QStringLiteral("@") + it.key();
    return opts;
}

QHash<QTableWidget *, class TypeComboDelegate *> &typeDelegateMap()
{
    static QHash<QTableWidget *, class TypeComboDelegate *> m;
    return m;
}

class TypeComboDelegate : public QStyledItemDelegate
{
public:
    explicit TypeComboDelegate(QStringList opts, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_opts(std::move(opts))
    {
    }

    void setOptions(const QStringList &opts) { m_opts = opts; }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *c = new QComboBox(parent);
        c->setEditable(false);            // 只能选
        for (const QString &o : m_opts)
            c->addItem(o);
        return c;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *c = qobject_cast<QComboBox *>(editor);
        if (!c)
            return;
        const QString cur = index.data(Qt::EditRole).toString();
        int i = c->findText(cur);
        if (i < 0)
        {
            // 模板里已有、但不在选项里的类型仍要能选到，避免一打开就被清空
            c->addItem(cur);
            i = c->count() - 1;
        }
        c->setCurrentIndex(i);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *c = qobject_cast<QComboBox *>(editor);
        if (!c)
            return;
        model->setData(index, c->currentText(), Qt::EditRole);
    }

private:
    QStringList m_opts;
};

void setupSegTable(QTableWidget *t, bool compact = false)
{
    t->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("类型"),
                                  QStringLiteral("长度"), QStringLiteral("取值"),
                                  QStringLiteral("单位"), QStringLiteral("其它键")});
    t->horizontalHeader()->setStretchLastSection(true);
    // 紧凑档给变体段表：它只占右半栏，列宽按比例收，避免出现横向滚动条。
    t->setColumnWidth(SegField, compact ? 118 : 160);
    t->setColumnWidth(SegType, compact ? 62 : 96);
    t->setColumnWidth(SegCount, compact ? 52 : 80);
    t->setColumnWidth(SegValue, compact ? 58 : 90);
    t->setColumnWidth(SegUnit, compact ? 54 : 80);
    // 类型列常驻下拉（点击即弹，不可输入），与段详情口径一致
    auto *typeDelegate = new TypeComboDelegate(typeOptionsWith({}), t);
    t->setItemDelegateForColumn(SegType, typeDelegate);
    typeDelegateMap().insert(t, typeDelegate);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setDragEnabled(true);
    t->setAcceptDrops(true);
    t->viewport()->setAcceptDrops(true);
    t->setDropIndicatorShown(true);
    t->setDragDropMode(QAbstractItemView::InternalMove);
    t->setDefaultDropAction(Qt::MoveAction);
}

// 段详情里用 "bit.offset" 这种路径读写嵌套键：留空即删除，删空的分组对象也一并清掉。
QJsonValue pathGet(const QJsonObject &o, const QString &path)
{
    QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QJsonValue cur = o;
    for (const QString &k : parts)
    {
        if (!cur.isObject())
            return {};
        cur = cur.toObject().value(k);
    }
    return cur;
}

void pathSet(QJsonObject &o, const QString &path, const QJsonValue &v)
{
    QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;
    const QString key = parts.takeFirst();
    if (parts.isEmpty())
    {
        if (v.isUndefined() || v.isNull())
            o.remove(key);
        else
            o.insert(key, v);
        return;
    }
    QJsonObject sub = o.value(key).toObject();
    pathSet(sub, parts.join(QLatin1Char('.')), v);
    if (sub.isEmpty())
        o.remove(key);
    else
        o.insert(key, sub);
}

// 只读摘要：列出除"字段/长度/类型/取值"以外的键。
QString otherKeysText(const QJsonObject &seg)
{
    static const QHash<QString, QString> kCn = {
        {QStringLiteral("emit"), QStringLiteral("输出")},
        {QStringLiteral("bit"), QStringLiteral("位域")},
        {QStringLiteral("length"), QStringLiteral("长度")},
        {QStringLiteral("scale"), QStringLiteral("缩放")},
        {QStringLiteral("offset"), QStringLiteral("偏移")},
        {QStringLiteral("endian"), QStringLiteral("字节序")},
        {QStringLiteral("range"), QStringLiteral("范围")},
        {QStringLiteral("enum"), QStringLiteral("枚举")},
        {QStringLiteral("digest"), QStringLiteral("校验")},
        {QStringLiteral("checks"), QStringLiteral("校验规则")},
        {QStringLiteral("pattern"), QStringLiteral("正则")},
        {QStringLiteral("if"), QStringLiteral("条件")},
        {QStringLiteral("repeat"), QStringLiteral("重复")},
        {QStringLiteral("until"), QStringLiteral("终止")},
        {QStringLiteral("derive"), QStringLiteral("派生")},
        {QStringLiteral("transform"), QStringLiteral("变换")},
        {QStringLiteral("empty"), QStringLiteral("空值")},
        {QStringLiteral("as"), QStringLiteral("类型覆盖")},
        {QStringLiteral("base"), QStringLiteral("进制")},
        {QStringLiteral("fixed"), QStringLiteral("小数位")},
        {QStringLiteral("signed"), QStringLiteral("负号")},
        {QStringLiteral("sci"), QStringLiteral("科学计数")},
        {QStringLiteral("key"), QStringLiteral("键名")},
        {QStringLiteral("delimiter"), QStringLiteral("边界")},
        {QStringLiteral("literal"), QStringLiteral("字面量")},
        {QStringLiteral("rest"), QStringLiteral("剩余文本")},
        {QStringLiteral("width"), QStringLiteral("定宽")},
        {QStringLiteral("repeater"), QStringLiteral("重复符")},
        {QStringLiteral("on_mismatch"), QStringLiteral("不符策略")},
        {QStringLiteral("description"), QStringLiteral("说明")},
    };
    QStringList out;
    for (auto it = seg.begin(); it != seg.end(); ++it)
    {
        const QString k = it.key();
        if (k == QLatin1String("name") || k == QLatin1String("type") ||
            k == QLatin1String("count") || k == QLatin1String("expect") ||
            k == QLatin1String("unit"))
            continue;
        out << kCn.value(k, k);
    }
    // 稳定顺序：先输出语义，再结构与校验，最后文本类，读起来像一句话
    static const QStringList kOrder = {
        QStringLiteral("输出"), QStringLiteral("位域"), QStringLiteral("缩放"),
        QStringLiteral("偏移"), QStringLiteral("字节序"), QStringLiteral("范围"),
        QStringLiteral("枚举"), QStringLiteral("校验"), QStringLiteral("校验规则"),
        QStringLiteral("长度"), QStringLiteral("条件"), QStringLiteral("重复"),
        QStringLiteral("终止"), QStringLiteral("派生"), QStringLiteral("变换"),
    };
    QStringList ordered;
    for (const QString &k : kOrder)
        if (out.contains(k))
            ordered << k;
    for (const QString &k : out)
        if (!ordered.contains(k))
            ordered << k;
    return ordered.join(QStringLiteral(" · "));
}


// 段详情用：标量文本 ↔ JSON（不依赖类私有方法，供匿名命名空间内使用）
QJsonValue parseScalarText(const QString &t)
{
    if (t.isEmpty())
        return {};
    const QString v = t.trimmed();
    if (v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    {
        bool ok = false;
        const qlonglong n = v.mid(2).toLongLong(&ok, 16);
        if (ok)
            return QJsonValue(double(n));
    }
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (ok)
        return QJsonValue(d);
    return QJsonValue(v);
}

QString scalarText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
    {
        const double d = v.toDouble();
        if (qFuzzyCompare(d + 1.0, qRound64(d) + 1.0))
            return QString::number(qRound64(d));
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString();
}

enum SegKind { SKText, SKInt, SKDouble, SKTri, SKEnum, SKFields, SKInject, SKEnumMap, SKRange };

struct SegKeyDef
{
    QString path;
    QString label;
    SegKind kind;
    QString hint;
    QStringList opts;
};

// 其余属性：折叠在「更多属性」里，用下拉挑，不用背键名。
const QVector<SegKeyDef> &segKeyDefs()
{
    static const QVector<SegKeyDef> defs = [] {
        QVector<SegKeyDef> v;
        auto add = [&v](const char *path, const char *label, SegKind kind, const char *hint,
                        QStringList opts = {}) {
            v.push_back({QString::fromLatin1(path), QString::fromUtf8(label), kind,
                         QString::fromUtf8(hint), opts});
        };
        add("endian", "字节序", SKEnum, "覆盖协议全局的字节序", {"little", "big"});
        add("on_mismatch", "不符策略", SKEnum, "期望值不匹配时怎么处理",
            {"reject", "warn", "skip"});
        add("scale", "比例", SKDouble, "原始值 × 比例 = 物理值，如 0.25；不填=1");
        add("offset", "偏移", SKDouble, "物理值 = 原始 × 比例 + 偏移；不填=0");
        add("bit.base", "位域容器", SKText, "在哪个段里切位；不填=整帧起点");
        add("bit.offset", "位偏移", SKInt, "单位是 bit，不是字节");
        add("bit.width", "位宽", SKInt, "取多少位，1–64");
        add("bit.order", "位序", SKEnum, "lsb0=Intel（低位在前），msb0=Motorola",
            {"lsb0", "msb0"});
        add("length.fields", "长度覆盖段", SKFields, "长度等于后面哪些段的合计，逗号分隔");
        add("length.unit", "长度单位", SKEnum, "bytes=字节；elements=元素数",
            {"bytes", "elements"});
        add("length.expr", "长度表达式", SKText, "直接写表达式，如 payload+crc");
        add("digest.alg", "校验算法", SKEnum, "内置算法，选一个",
            {"xor8", "sum8", "crc8", "crc16", "crc16_modbus", "crc16_x25", "crc24q",
             "crc32", "crc32c", "nmea_xor"});
        add("digest.over", "校验参与段", SKFields, "参与校验的段，可用 a:b 表示区间");
        add("digest.width", "校验位宽", SKInt, "默认 8；crc32 选 32");
        add("digest.inject", "追加常量", SKInject, "算完追加的常量字节，如 0x1D；逗号分隔");
        add("digest.poly", "多项式", SKText, "覆盖算法内建多项式，如 0x07");
        add("digest.init", "初值", SKText, "如 0xFFFF");
        add("digest.xorout", "结果异或", SKText, "如 0xFFFF");
        add("digest.refin", "输入反射", SKTri, "不填=false；按数据手册选");
        add("digest.refout", "输出反射", SKTri, "不填=false；一般与输入反射一致");
        add("enum", "枚举", SKEnumMap, "每行「值=名字」，如 0=OFF");
        add("range", "有效范围", SKRange, "合法取值范围，超出会告警");
        add("pattern", "正则校验", SKText, "只校验不切分，如 ^[0-9]+$");
        add("if", "生效条件", SKText, "表达式为假时整段跳过，如 msgid>10");
        add("repeat", "重复方式", SKEnum, "elems=按个数；eos=读到结尾；until=读到条件成立",
            {"elems", "eos", "until"});
        add("until", "终止条件", SKText, "repeat=until 时用，如 b>=20");
        add("derive", "派生字段", SKText, "由前序段算出来，不占字节");
        add("literal", "字面量", SKText, "必须逐字符匹配，如 $ 或 ,");
        add("delimiter", "结束边界", SKText, "blank=空格/Tab；也可写逗号、分号");
        add("rest", "取剩余原文", SKTri, "true=整段剩余文本不再切分");
        add("key", "键名", SKText, "key=value 里的键名，如 id");
        add("width", "定宽字符", SKInt, "按固定字符数切分");
        add("base", "进制", SKEnum, "数字按几进制解析", {"2", "8", "10", "16"});
        add("fixed", "小数位", SKInt, "定点小数位数，如 2 表示 12.34");
        add("signed", "允许负号", SKTri, "不填=false；拒绝负号");
        add("sci", "科学计数", SKTri, "不填=false；拒绝 1.2e3");
        add("as", "数值解释", SKEnum, "按指定类型解释数值",
            {"f64", "f32", "i64", "i32", "u64", "u32"});
        return v;
    }();
    return defs;
}

// 选择型一律下拉且不可输入；统一在这里造控件，避免各处风格漂移。
QWidget *makeSegEditor(SegKind kind, const QStringList &opts)
{
    if (kind == SKEnumMap)
    {
        auto *e = new QPlainTextEdit;
        e->setMaximumHeight(96);
        e->setPlaceholderText(QStringLiteral("0=OFF\n1=ON"));
        return e;
    }
    if (kind == SKRange)
    {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        auto *a = new QLineEdit;
        a->setObjectName(QStringLiteral("min"));
        a->setPlaceholderText(QStringLiteral("最小"));
        auto *b = new QLineEdit;
        b->setObjectName(QStringLiteral("max"));
        b->setPlaceholderText(QStringLiteral("最大"));
        l->addWidget(a);
        l->addWidget(new QLabel(QStringLiteral("~")));
        l->addWidget(b);
        return w;
    }
    if (kind == SKEnum || kind == SKTri)
    {
        auto *c = new QComboBox;
        c->setEditable(false);
        c->addItem(QStringLiteral("（不填）"));
        for (const QString &o : opts)
            c->addItem(o);
        return c;
    }
    auto *le = new QLineEdit;
    if (kind == SKInt)
        le->setValidator(new QIntValidator(-1000000, 1000000, le));
    return le;
}

QJsonValue segEditorValue(QWidget *w, SegKind kind)
{
    if (auto *c = qobject_cast<QComboBox *>(w))
    {
        if (c->currentIndex() <= 0)
            return {};
        return kind == SKTri ? QJsonValue(c->currentText() == QLatin1String("true"))
                             : QJsonValue(c->currentText());
    }
    if (auto *e = qobject_cast<QPlainTextEdit *>(w))
    {
        const QStringList lines = e->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
        QJsonObject m;
        for (const QString &ln : lines)
        {
            const int eq = ln.indexOf(QLatin1Char('='));
            if (eq > 0)
                m.insert(ln.left(eq).trimmed(), ln.mid(eq + 1).trimmed());
        }
        if (m.isEmpty())
            return {};
        return m;
    }
    if (kind == SKRange)
    {
        const auto *a = w->findChild<QLineEdit *>(QStringLiteral("min"));
        const auto *b = w->findChild<QLineEdit *>(QStringLiteral("max"));
        if (!a || !b || a->text().trimmed().isEmpty() || b->text().trimmed().isEmpty())
            return {};
        QJsonArray arr;
        arr.append(a->text().trimmed().toDouble());
        arr.append(b->text().trimmed().toDouble());
        return arr;
    }
    if (auto *le = qobject_cast<QLineEdit *>(w))
    {
        const QString t = le->text().trimmed();
        if (t.isEmpty())
            return {};
        if (kind == SKInt)
            return QJsonValue(t.toInt());
        if (kind == SKDouble)
            return QJsonValue(t.toDouble());
        if (kind == SKFields)
        {
            QJsonArray a;
            for (const QString &x : t.split(QLatin1Char(','), Qt::SkipEmptyParts))
                a.append(x.trimmed());
            return a;
        }
        if (kind == SKInject)
        {
            QJsonArray a;
            for (const QString &x : t.split(QLatin1Char(','), Qt::SkipEmptyParts))
                a.append(parseScalarText(x));
            return a;
        }
        return parseScalarText(t);
    }
    return {};
}

void segEditorSetValue(QWidget *w, SegKind kind, const QJsonValue &v)
{
    if (auto *c = qobject_cast<QComboBox *>(w))
        c->setCurrentText(scalarText(v));
    else if (auto *e = qobject_cast<QPlainTextEdit *>(w))
    {
        if (!v.isObject())
            return;
        QStringList lines;
        const QJsonObject m = v.toObject();
        for (auto it = m.begin(); it != m.end(); ++it)
            lines << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
        e->setPlainText(lines.join(QChar('\n')));
    }
    else if (kind == SKRange)
    {
        if (!v.isArray())
            return;
        auto *a = w->findChild<QLineEdit *>(QStringLiteral("min"));
        auto *b = w->findChild<QLineEdit *>(QStringLiteral("max"));
        const QJsonArray arr = v.toArray();
        if (a && arr.size() > 0)
            a->setText(scalarText(arr.at(0)));
        if (b && arr.size() > 1)
            b->setText(scalarText(arr.at(1)));
    }
    else if (auto *le = qobject_cast<QLineEdit *>(w))
        le->setText(scalarText(v));
}

} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(QWidget *parent, AppContext *appCtx,
                                          nextpilot::plotscope::core::IIntentExecutor *executor)
    : QDialog(parent), m_appCtx(appCtx), m_executor(executor)
{
    setWindowTitle(QStringLiteral("协议编辑器"));
    resize(1060, 900);  // 与老编辑器同尺寸：段表 + 变体段表都要留出足够行高

    // --- 顶部：文件操作 ---
    auto *top = new QHBoxLayout;
    auto *newBtn = new QPushButton(QStringLiteral("新建"));
    auto *openBtn = new QPushButton(QStringLiteral("打开"));
    m_saveBtn = new QPushButton(QStringLiteral("保存"));
    auto *validateBtn = new QPushButton(QStringLiteral("校验"));
    auto *checksumBtn = new QPushButton(QStringLiteral("Checksum 变换…"));
    checksumBtn->setToolTip(QStringLiteral("为 checksum 配置算子 / 数学表达式 / Lua（按能力过滤 + 在线调试）"));
    top->addWidget(newBtn);
    top->addWidget(openBtn);
    top->addWidget(m_saveBtn);
    top->addWidget(validateBtn);
    top->addWidget(checksumBtn);
    top->addStretch(1);
    connect(checksumBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::configureChecksumTransform);

    // --- 主体：左「协议 / JSON」页，右 校验与试算 ---
    auto *body = new QSplitter(Qt::Horizontal);

    auto *left = new QWidget;
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget;
    buildDesignPages();  // 顶层字段 / 帧结构 / 子协议

    auto *jsonPage = new QWidget;
    auto *jsonLay = new QVBoxLayout(jsonPage);
    jsonLay->setContentsMargins(0, 0, 0, 0);
    m_json = new QPlainTextEdit;
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    jsonLay->addWidget(m_json, 1);
    m_jsonTab = m_tabs->addTab(jsonPage, QStringLiteral("JSON"));

    leftLay->addWidget(m_tabs, 1);

    auto *right = new QWidget;
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    m_validation = new QLabel(QStringLiteral("尚未校验"));
    m_validation->setWordWrap(true);
    auto *valTitle = new QLabel(QStringLiteral("校验结果"));
    valTitle->setStyleSheet(QStringLiteral("font-weight:600;"));
    rightLay->addWidget(valTitle);
    rightLay->addWidget(m_validation);

    auto *hexTitle = new QLabel(QStringLiteral("样例帧（hex）"));
    hexTitle->setStyleSheet(QStringLiteral("font-weight:600; margin-top:8px;"));
    rightLay->addWidget(hexTitle);
    m_hex = new QLineEdit;
    m_hex->setPlaceholderText(QStringLiteral("AA 55 0C 00 01"));
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_hex->setClearButtonEnabled(true);
    rightLay->addWidget(m_hex);
    auto *previewBtn = new QPushButton(QStringLiteral("试算单帧"));
    rightLay->addWidget(previewBtn);
    // 试算结果用表格而不是文本框：每行一个字段，字段 / 偏移 / 值 三列严格对齐。
    // 两列足够：字段名 + 完整结果。偏移列对 bit 切片段全是 0，留着只是噪声。
    m_preview = new QTableWidget(0, 2);
    m_preview->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("值")});
    m_preview->horizontalHeader()->setStretchLastSection(true);
    m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_preview->setColumnWidth(0, 110);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_preview->setSelectionMode(QAbstractItemView::SingleSelection);
    m_preview->setAlternatingRowColors(true);
    m_preview->setWordWrap(false);
    rightLay->addWidget(m_preview, 1);

    right->setFixedWidth(360);
    body->addWidget(left);
    body->addWidget(right);
    body->setStretchFactor(0, 1);
    body->setStretchFactor(1, 0);

    // --- 底部 ---
    m_status = new QLabel(QStringLiteral("新建协议"));
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    bottom->addWidget(closeBtn);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(body, 1);
    root->addLayout(bottom);

    connect(newBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::newDoc);
    connect(openBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::openDoc);
    connect(m_saveBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::saveDoc);
    connect(validateBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::validateDoc);
    connect(previewBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::previewFrame);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // 切页签即"落表"：去 JSON 页先把控件写成文本；回协议页再从文本重建。
    // 这样两边永远只有一份真数据。
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncing)
            return;
        // 离开编辑页 -> 先把控件落成 JSON；回到编辑页 -> 再从 JSON 重建。
        // 这样无论在哪一页改，另一页看到的都是同一份内容。
        if (m_dirty)
            commitDesign();
        if (index != m_jsonTab)
            refreshFromJson();
    });

    newDoc();
    applyTheme();
}

ProtocolEditorDialog::~ProtocolEditorDialog() = default;

// ---------------------------------------------------------------- 协议页布局

void ProtocolEditorDialog::buildDesignPages()
{
    // ---- 页 1：顶层字段（协议级只写一次的东西） ----
    auto *fieldPage = new QWidget;
    auto *fieldLay = new QVBoxLayout(fieldPage);
    fieldLay->setContentsMargins(8, 8, 8, 8);

    auto *head = new QGroupBox(QStringLiteral("顶层字段"), fieldPage);
    // 单列表单在桌面上会被拉成 700px 的长条；两列网格更紧凑，也更好扫读。
    auto *grid = new QGridLayout(head);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    // 列宽必须显式给：跨列的行不会把第 1 列撑开，否则数字框只剩 50px 宽。
    grid->setColumnMinimumWidth(0, 78);
    grid->setColumnMinimumWidth(1, 210);
    grid->setColumnMinimumWidth(3, 190);
    grid->setColumnStretch(1, 3);
    grid->setColumnStretch(3, 2);
    m_name = new QLineEdit(head);
    m_desc = new QLineEdit(head);
    m_mode = new QComboBox(head);
    m_mode->addItems({QStringLiteral("bin"), QStringLiteral("text")});
    m_layout = new QComboBox(head);
    m_layout->addItems({QStringLiteral("sequential"), QStringLiteral("line"),
                        QStringLiteral("csv"), QStringLiteral("kv")});
    m_maxsize = new QSpinBox(head);
    m_maxsize->setRange(0, 65535);
    m_maxsize->setSpecialValueText(QStringLiteral("未设"));
    // 数字框的水平策略默认按 sizeHint 走，不会填满格子；显式改成 Expanding 才和下拉同宽。
    m_maxsize->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_maxsize->setMinimumWidth(110);
    m_endian = new QComboBox(head);
    m_endian->addItems({QStringLiteral("little"), QStringLiteral("big")});
    m_tsExpr = new QLineEdit(head);
    m_tsExpr->setPlaceholderText(QStringLiteral("如 time_s 或 day*24*3600 + sec + ms/1000"));
    m_tsUnit = new QComboBox(head);
    m_tsUnit->addItems({QString(), QStringLiteral("s"), QStringLiteral("ms"),
                        QStringLiteral("us"), QStringLiteral("ns")});
    auto *tsRow = new QHBoxLayout;
    tsRow->setContentsMargins(0, 0, 0, 0);
    tsRow->addWidget(m_tsExpr, 1);
    tsRow->addWidget(new QLabel(QStringLiteral("单位"), head));
    tsRow->addWidget(m_tsUnit);
    auto *tsWrap = new QWidget(head);
    tsWrap->setLayout(tsRow);

    m_terminator = new QLineEdit(head);
    m_terminator->setPlaceholderText(QStringLiteral("text 帧终止标记，如 \\r\\n（可留空）"));

    auto *verLabel = new QLabel(QStringLiteral("psproto 2 · v1 键已删除，不做兼容映射"), head);
    verLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    grid->addWidget(new QLabel(QStringLiteral("名称")), 0, 0);
    grid->addWidget(m_name, 0, 1, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("描述")), 1, 0);
    grid->addWidget(m_desc, 1, 1, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("模式")), 2, 0);
    grid->addWidget(m_mode, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("布局")), 2, 2);
    grid->addWidget(m_layout, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("最大帧长")), 3, 0);
    grid->addWidget(m_maxsize, 3, 1);
    grid->addWidget(new QLabel(QStringLiteral("字节序")), 3, 2);
    grid->addWidget(m_endian, 3, 3);
    grid->addWidget(new QLabel(QStringLiteral("时间戳")), 4, 0);
    grid->addWidget(tsWrap, 4, 1, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("帧终止")), 5, 0);
    grid->addWidget(m_terminator, 5, 1, 1, 3);
    grid->addWidget(verLabel, 6, 0, 1, 4);
    fieldLay->addWidget(head);
    fieldLay->addStretch(1);
    m_tabs->addTab(fieldPage, QStringLiteral("顶层字段"));

    // ---- 页 2：帧结构（段表，协议的主体） ----
    auto *framePage = new QWidget;
    auto *frameLay = new QVBoxLayout(framePage);
    frameLay->setContentsMargins(8, 8, 8, 8);

    // 增删只在表格上发生（底部「＋ 添加字段」/ 右键 / Delete），顶部不再放一对按钮。
    auto *segHint = new QLabel(
        QStringLiteral("类型在前、长度在后；长度=元素个数（可写前序段名或表达式）。"
                       "拖动行调顺序，双击行开「段详情」— 里面每个属性都有说明和可选值。"),
        framePage);
    segHint->setWordWrap(true);
    segHint->setStyleSheet(QStringLiteral("color:#666;"));
    frameLay->addWidget(segHint);

    auto *segTable = new ReorderTable(0, SegColCount, framePage);
    m_segs = segTable;
    setupSegTable(m_segs);
    static_cast<ReorderTable *>(m_segs)->onMove = [this](int from, int to) {
        moveTableRow(m_segs, from, to);
        markDirty();
    };
    frameLay->addWidget(m_segs, 1);
    auto *segAddRowBtn = new QPushButton(QStringLiteral("＋ 添加字段"), framePage);
    segAddRowBtn->setFixedHeight(24);
    segAddRowBtn->setFixedWidth(104);
    segAddRowBtn->setToolTip(QStringLiteral("在表尾追加一行；插到中间用右键菜单"));
    auto *addBar = new QHBoxLayout;
    addBar->addWidget(segAddRowBtn);
    addBar->addStretch(1);
    frameLay->addLayout(addBar);
    m_tabs->addTab(framePage, QStringLiteral("帧结构"));

    // ---- 页 3：子协议（系列 + 变体段表） ----
    auto *subPage = new QWidget;
    auto *subLay = new QVBoxLayout(subPage);
    subLay->setContentsMargins(8, 8, 8, 8);

    m_seriesBox = new QGroupBox(QStringLiteral("系列协议（按选择段区分变体）"), subPage);
    auto *seriesLayout = new QVBoxLayout(m_seriesBox);
    m_seriesOn = new QCheckBox(QStringLiteral("启用"), m_seriesBox);
    seriesLayout->addWidget(m_seriesOn);

    auto *seriesHint = new QLabel(
        QStringLiteral("未启用时整帧按「帧结构」页的段表解析；启用后先读 selector 段，再按变体表切换到对应段表。"),
        m_seriesBox);
    seriesHint->setWordWrap(true);
    seriesHint->setStyleSheet(QStringLiteral("color:#8b949e;"));
    seriesLayout->addWidget(seriesHint);

    m_seriesBody = new QWidget(m_seriesBox);
    auto *bodyLay = new QVBoxLayout(m_seriesBody);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    auto *seriesTop = new QHBoxLayout;
    m_selector = new QLineEdit(m_seriesBody);
    m_selector->setPlaceholderText(QStringLiteral("selector，如 msgid"));
    m_payload = new QLineEdit(m_seriesBody);
    m_payload->setPlaceholderText(QStringLiteral("payload 段名"));
    m_fallback = new QComboBox(m_seriesBody);
    m_fallback->addItems({QStringLiteral("reject"), QStringLiteral("unknown")});
    seriesTop->addWidget(new QLabel(QStringLiteral("selector"), m_seriesBody));
    seriesTop->addWidget(m_selector, 1);
    seriesTop->addWidget(new QLabel(QStringLiteral("payload"), m_seriesBody));
    seriesTop->addWidget(m_payload);
    seriesTop->addWidget(new QLabel(QStringLiteral("fallback"), m_seriesBody));
    seriesTop->addWidget(m_fallback);
    bodyLay->addLayout(seriesTop);

    // 变体列表在左、变体段表在右：一屏内完成"选变体 → 编段"，不用上下滚动。
    auto *varArea = new QHBoxLayout;
    varArea->setSpacing(8);

    auto *varCol = new QWidget(m_seriesBody);
    varCol->setFixedWidth(248);
    auto *varColLay = new QVBoxLayout(varCol);
    varColLay->setContentsMargins(0, 0, 0, 0);
    auto *varBtnRow = new QHBoxLayout;
    auto *varAdd = new QPushButton(QStringLiteral("+ 变体"), varCol);
    auto *varDel = new QPushButton(QStringLiteral("− 变体"), varCol);
    varBtnRow->addWidget(varAdd);
    varBtnRow->addWidget(varDel);
    varBtnRow->addStretch(1);
    varColLay->addLayout(varBtnRow);

    m_variants = new QTableWidget(0, 2, varCol);
    m_variants->setHorizontalHeaderLabels({QStringLiteral("取值"), QStringLiteral("变体名")});
    m_variants->horizontalHeader()->setStretchLastSection(true);
    m_variants->setColumnWidth(0, 96);
    m_variants->setAlternatingRowColors(true);
    m_variants->verticalHeader()->setVisible(false);
    m_variants->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_variants->setSelectionMode(QAbstractItemView::SingleSelection);
    varColLay->addWidget(m_variants, 1);
    varArea->addWidget(varCol);

    auto *varSegCol = new QWidget(m_seriesBody);
    auto *varSegLay = new QVBoxLayout(varSegCol);
    varSegLay->setContentsMargins(0, 0, 0, 0);
    auto *varSegTitle = new QLabel(QStringLiteral("该变体的段表（选中左侧某行变体后编辑）"), varSegCol);
    varSegLay->addWidget(varSegTitle);
    auto *varSegTable = new ReorderTable(0, SegColCount, varSegCol);
    m_varSegs = varSegTable;
    setupSegTable(m_varSegs, true);
    static_cast<ReorderTable *>(m_varSegs)->onMove = [this](int from, int to) {
        moveTableRow(m_varSegs, from, to);
        storeVariantSegs();
        markDirty();
    };
    varSegLay->addWidget(m_varSegs, 1);
    auto *varSegAddRowBtn = new QPushButton(QStringLiteral("＋ 添加字段"), varSegCol);
    varSegAddRowBtn->setFixedHeight(24);
    varSegLay->addWidget(varSegAddRowBtn);
    varArea->addWidget(varSegCol, 1);

    bodyLay->addLayout(varArea, 1);
    seriesLayout->addWidget(m_seriesBody, 1);
    subLay->addWidget(m_seriesBox, 1);
    m_tabs->addTab(subPage, QStringLiteral("子协议"));

    // ---- 各页共用的动作 ----
    connect(segAddRowBtn, &QPushButton::clicked, this, [this] {
        addSegRowAt(m_segs, m_segs->rowCount());
    });
    connect(varSegAddRowBtn, &QPushButton::clicked, this, [this] {
        addSegRowAt(m_varSegs, m_varSegs->rowCount());
    });
    wireSegTable(m_segs);
    wireSegTable(m_varSegs);
    connect(varAdd, &QPushButton::clicked, this, &ProtocolEditorDialog::addVariantRow);
    connect(varDel, &QPushButton::clicked, this, &ProtocolEditorDialog::removeVariantRow);
    connect(m_variants, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_syncing || row == m_variantRow)
            return;
        storeVariantSegs();
        m_variantRow = row;
        reloadVariantSegs();
    });
    connect(m_segs, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) { markDirty(); });
    connect(m_varSegs, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        storeVariantSegs();
        markDirty();
    });
    // 双击段详情、右键增删、Delete 删除统一由 wireSegTable 装配（见上）。
}

// ---------------------------------------------------------------- 字面量

QString ProtocolEditorDialog::literalText(const QJsonValue &v)
{
    if (v.isDouble())
    {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isString())
        return v.toString();
    return QString();
}

QJsonValue ProtocolEditorDialog::parseLiteral(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    if (t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
        return QJsonValue(true);
    if (t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
        return QJsonValue(false);
    bool ok = false;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    {
        const auto n = t.mid(2).toLongLong(&ok, 16);
        if (ok)
            return QJsonValue(static_cast<double>(n));
    }
    const auto n = t.toLongLong(&ok, 10);
    if (ok)
        return QJsonValue(static_cast<double>(n));
    const double d = t.toDouble(&ok);
    if (ok)
        return QJsonValue(d);
    return QJsonValue(t);
}

// ---------------------------------------------------------------- 段表填充/读取

void ProtocolEditorDialog::fillSegTable(QTableWidget *table, const QJsonArray &segs)
{
    m_syncing = true;
    table->setRowCount(0);
    for (const QJsonValue &v : segs)
    {
        const QJsonObject seg = v.toObject();
        const int r = table->rowCount();
        table->insertRow(r);

        auto *fieldItem = new QTableWidgetItem(seg.value(QStringLiteral("name")).toString());
        storeObjectOnItem(fieldItem, seg);
        table->setItem(r, SegField, fieldItem);
        table->setItem(r, SegCount,
                       new QTableWidgetItem(literalText(seg.value(QStringLiteral("count")))));
        table->setItem(r, SegType,
                       new QTableWidgetItem(seg.value(QStringLiteral("type")).toString()));
        table->setItem(r, SegValue,
                       new QTableWidgetItem(literalText(seg.value(QStringLiteral("expect")))));
        table->setItem(r, SegUnit,
                       new QTableWidgetItem(seg.value(QStringLiteral("unit")).toString()));
        auto *other = new QTableWidgetItem(otherKeysText(seg));
        QStringList rawKeys;
        for (auto it = seg.begin(); it != seg.end(); ++it)
        {
            const QString k = it.key();
            if (k == QLatin1String("name") || k == QLatin1String("type") ||
                k == QLatin1String("count") || k == QLatin1String("expect") ||
                k == QLatin1String("unit"))
                continue;
            rawKeys << k;
        }
        other->setToolTip(rawKeys.join(QStringLiteral(", ")));
        other->setFlags(other->flags() & ~Qt::ItemIsEditable);
        table->setItem(r, SegOther, other);
    }
    m_syncing = false;
}

QJsonArray ProtocolEditorDialog::segTableToJson(QTableWidget *table) const
{
    QJsonArray arr;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        auto *fieldItem = table->item(r, SegField);
        const QString name = fieldItem ? fieldItem->text().trimmed() : QString();
        if (name.isEmpty())
            continue;

        // 以原段对象为底，保住表格没覆盖的键（bit / transform / digest / scale / unit / …）。
        QJsonObject seg = objectFromItem(fieldItem);
        seg.insert(QStringLiteral("name"), name);

        const QString type =
            table->item(r, SegType) ? table->item(r, SegType)->text().trimmed() : QString();
        seg.insert(QStringLiteral("type"), type.isEmpty() ? QStringLiteral("u8") : type);

        const QJsonValue count = parseLiteral(
            table->item(r, SegCount) ? table->item(r, SegCount)->text() : QString());
        if (count.isUndefined())
            seg.remove(QStringLiteral("count"));
        else
            seg.insert(QStringLiteral("count"), count);

        const QJsonValue expect = parseLiteral(
            table->item(r, SegValue) ? table->item(r, SegValue)->text() : QString());
        if (expect.isUndefined())
            seg.remove(QStringLiteral("expect"));
        else
            seg.insert(QStringLiteral("expect"), expect);

        const QString unit =
            table->item(r, SegUnit) ? table->item(r, SegUnit)->text().trimmed() : QString();
        if (unit.isEmpty())
            seg.remove(QStringLiteral("unit"));
        else
            seg.insert(QStringLiteral("unit"), unit);

        arr.append(seg);
    }
    return arr;
}

void ProtocolEditorDialog::fillVariantsTable(const QJsonArray &variants)
{
    m_syncing = true;
    m_variants->setRowCount(0);
    for (const QJsonValue &v : variants)
    {
        const QJsonObject var = v.toObject();
        const int r = m_variants->rowCount();
        m_variants->insertRow(r);

        // 匹配值：value 单值或 values 数组，逗号分隔。
        QString match;
        if (var.contains(QStringLiteral("values")))
        {
            QStringList parts;
            for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                parts << literalText(x);
            match = parts.join(QStringLiteral(","));
        }
        else if (var.contains(QStringLiteral("value")))
        {
            match = literalText(var.value(QStringLiteral("value")));
        }
        m_variants->setItem(r, 0, new QTableWidgetItem(match));

        auto *nameItem = new QTableWidgetItem(var.value(QStringLiteral("name")).toString());
        storeObjectOnItem(nameItem, var);
        m_variants->setItem(r, 1, nameItem);
    }
    m_variantRow = -1;
    m_syncing = false;
}

void ProtocolEditorDialog::reloadVariantSegs()
{
    if (m_variantRow < 0)
    {
        m_syncing = true;
        m_varSegs->setRowCount(0);
        m_syncing = false;
        return;
    }
    const QJsonObject var = objectFromItem(m_variants->item(m_variantRow, 1));
    fillSegTable(m_varSegs, var.value(QStringLiteral("segments")).toArray());
}

void ProtocolEditorDialog::storeVariantSegs()
{
    if (m_variantRow < 0 || m_variantRow >= m_variants->rowCount())
        return;
    QJsonObject var = objectFromItem(m_variants->item(m_variantRow, 1));
    var.insert(QStringLiteral("segments"), segTableToJson(m_varSegs));
    storeObjectOnItem(m_variants->item(m_variantRow, 1), var);
}

// ---------------------------------------------------------------- 变体增删

void ProtocolEditorDialog::addVariantRow()
{
    storeVariantSegs();
    const int r = m_variants->rowCount();
    m_syncing = true;
    m_variants->insertRow(r);
    m_variants->setItem(r, 0, new QTableWidgetItem(QStringLiteral("0")));
    const QString name = QStringLiteral("variant%1").arg(r + 1);
    auto *nameItem = new QTableWidgetItem(name);
    storeObjectOnItem(nameItem, QJsonObject{{QStringLiteral("name"), name}});
    m_variants->setItem(r, 1, nameItem);
    m_syncing = false;

    m_variantRow = r;
    m_variants->setCurrentCell(r, 0);
    m_syncing = true;
    m_varSegs->setRowCount(0);
    m_syncing = false;
    markDirty();
}

void ProtocolEditorDialog::removeVariantRow()
{
    const int r = m_variants->currentRow();
    if (r < 0)
        return;
    m_syncing = true;
    m_variants->removeRow(r);
    m_syncing = false;
    m_variantRow = -1;
    reloadVariantSegs();
    if (m_variants->rowCount() > 0)
        m_variants->setCurrentCell(std::min(r, m_variants->rowCount() - 1), 0);
    markDirty();
}

// ---------------------------------------------------------------- 段增删与详情

void ProtocolEditorDialog::addSegRowAt(QTableWidget *table, int insertAt)
{
    const int r = std::max(0, std::min(insertAt, table->rowCount()));
    table->insertRow(r);
    table->setItem(r, SegField, new QTableWidgetItem(QStringLiteral("field%1").arg(r + 1)));
    table->setItem(r, SegType, new QTableWidgetItem(QStringLiteral("u8")));
    table->setItem(r, SegCount, new QTableWidgetItem(QStringLiteral("1")));
    table->setItem(r, SegValue, new QTableWidgetItem(QString()));
    table->setItem(r, SegUnit, new QTableWidgetItem(QString()));
    auto *other = new QTableWidgetItem(QString());
    other->setFlags(other->flags() & ~Qt::ItemIsEditable);
    table->setItem(r, SegOther, other);
    table->setCurrentCell(r, SegField);
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

void ProtocolEditorDialog::removeSegRow(QTableWidget *table, int row)
{
    const int r = row >= 0 ? row : table->currentRow();
    if (r < 0)
        return;
    m_syncing = true;
    table->removeRow(r);
    m_syncing = false;
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

void ProtocolEditorDialog::wireSegTable(QTableWidget *table)
{
    // 增删只在表格上发生：底部「＋ 添加字段」是主入口，右键与 Delete 是补充；
    // 顺序靠拖拽，所以不再提供上移/下移按钮（按钮永远不如直接拖快）。
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QWidget::customContextMenuRequested, this, [this, table](const QPoint &pos) {
        const int row = table->indexAt(pos).row();
        QMenu menu(this);
        QAction *insBefore = menu.addAction(QStringLiteral("在此行前插入字段"));
        QAction *insAfter = menu.addAction(QStringLiteral("在此行后插入字段"));
        QAction *delAction = table->rowCount() ? menu.addAction(QStringLiteral("删除该字段"))
                                              : nullptr;
        QAction *detail = table->rowCount() ? menu.addAction(QStringLiteral("段详情…")) : nullptr;
        delAction ? delAction->setEnabled(row >= 0) : void();
        detail ? detail->setEnabled(row >= 0) : void();
        QAction *chosen = menu.exec(table->viewport()->mapToGlobal(pos));
        if (chosen == insBefore)
            addSegRowAt(table, row < 0 ? table->rowCount() : row);
        else if (chosen == insAfter)
            addSegRowAt(table, row < 0 ? table->rowCount() : row + 1);
        else if (chosen == delAction && row >= 0)
            removeSegRow(table, row);
        else if (chosen == detail && row >= 0)
            openSegmentDetail(table, row);
    });

    auto *delKey = new QShortcut(QKeySequence::Delete, table);
    delKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(delKey, &QShortcut::activated, this, [this, table] {
        const int row = table->currentRow();
        if (row >= 0)
            removeSegRow(table, row);
    });

    // 双击行的任意位置 → 段详情（其它键的入口）
    connect(table, &QTableWidget::cellDoubleClicked, this, [this, table](int row, int) {
        openSegmentDetail(table, row);
    });
}

void ProtocolEditorDialog::openSegmentDetail(QTableWidget *table, int row)
{
    if (row < 0)
        return;

    QJsonObject seg = objectFromItem(table->item(row, SegField));
    auto textOf = [table, row](int col) {
        const auto *it = table->item(row, col);
        return it ? it->text().trimmed() : QString();
    };
    seg.insert(QStringLiteral("name"), textOf(SegField));
    const QString type = textOf(SegType);
    seg.insert(QStringLiteral("type"), type.isEmpty() ? QStringLiteral("u8") : type);
    const QJsonValue count = parseLiteral(textOf(SegCount));
    if (count.isUndefined())
        seg.remove(QStringLiteral("count"));
    else
        seg.insert(QStringLiteral("count"), count);
    const QJsonValue expect = parseLiteral(textOf(SegValue));
    if (expect.isUndefined())
        seg.remove(QStringLiteral("expect"));
    else
        seg.insert(QStringLiteral("expect"), expect);
    if (textOf(SegUnit).isEmpty())
        seg.remove(QStringLiteral("unit"));
    else
        seg.insert(QStringLiteral("unit"), textOf(SegUnit));

    struct Bind
    {
        QString path;
        QWidget *editor;
        SegKind kind;
        QWidget *row;
    };
    QVector<Bind> binds;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("段详情 · %1").arg(textOf(SegField)));
    dlg.resize(700, 430);
    auto *lay = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(QStringLiteral(
        "常用属性直接改；其它属性点「更多属性」，或从下拉里挑一个再加上。下拉只能选，不用手写。"));
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color:#666;"));
    lay->addWidget(intro);

    auto addBind = [&binds](QWidget *holder, const QString &path, QWidget *editor, SegKind k) {
        binds.push_back({path, editor, k, holder});
    };

    // ---- 基本属性：两列平铺 ----
    auto *mainBox = new QGroupBox(QStringLiteral("基本属性"));
    auto *grid = new QGridLayout(mainBox);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnMinimumWidth(1, 150);
    grid->setColumnMinimumWidth(3, 150);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    int gi = 0, gj = 0;
    auto addMain = [&](const QString &label, const QString &path, SegKind kind,
                       const QString &hint, const QStringList &opts) {
        auto *lab = new QLabel(label);
        lab->setStyleSheet(QStringLiteral("color:#555;"));
        QWidget *editor = makeSegEditor(kind, opts);
        editor->setToolTip(hint);
        if (auto *le = qobject_cast<QLineEdit *>(editor))
            le->setPlaceholderText(hint);
        if (auto *c = qobject_cast<QComboBox *>(editor))
        {
            const QString cur = scalarText(pathGet(seg, path));
            if (!cur.isEmpty() && c->findText(cur) < 0)
                c->addItem(cur);  // 已有值不在选项里也保留，避免清空
        }
        auto *holder = new QWidget;
        auto *h = new QHBoxLayout(holder);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(editor);
        grid->addWidget(lab, gi, gj * 2);
        grid->addWidget(holder, gi, gj * 2 + 1);
        addBind(holder, path, editor, kind);
        if (++gj == 2)
        {
            gj = 0;
            ++gi;
        }
    };

    QStringList typeOpts = {QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"),
                            QStringLiteral("i16"), QStringLiteral("u32"), QStringLiteral("i32"),
                            QStringLiteral("u64"), QStringLiteral("i64"), QStringLiteral("f32"),
                            QStringLiteral("f64"), QStringLiteral("bool"), QStringLiteral("bcd"),
                            QStringLiteral("char"), QStringLiteral("string"),
                            QStringLiteral("number"), QStringLiteral("rest"),
                            QStringLiteral("time")};
    const QJsonObject types = QJsonDocument::fromJson(m_json->toPlainText().toUtf8())
                                  .object().value(QStringLiteral("types")).toObject();
    for (auto it = types.begin(); it != types.end(); ++it)
        typeOpts << QStringLiteral("@") + it.key();

    addMain(QStringLiteral("字段名"), QStringLiteral("name"), SKText,
            QStringLiteral("唯一段名，如 rpm"), {});
    addMain(QStringLiteral("类型"), QStringLiteral("type"), SKEnum,
            QStringLiteral("字段的数据类型"), typeOpts);
    addMain(QStringLiteral("个数"), QStringLiteral("count"), SKText,
            QStringLiteral("元素个数，留空=1；可填前序段名"), {});
    addMain(QStringLiteral("期望值"), QStringLiteral("expect"), SKText,
            QStringLiteral("常量匹配：170 或 0xAA"), {});
    addMain(QStringLiteral("单位"), QStringLiteral("unit"), SKText,
            QStringLiteral("物理单位，如 rpm / degC"), {});
    addMain(QStringLiteral("输出"), QStringLiteral("emit"), SKEnum,
            QStringLiteral("channel=画曲线；aux=辅助；none=只校验"),
            {QStringLiteral("channel"), QStringLiteral("aux"), QStringLiteral("none")});
    addMain(QStringLiteral("说明"), QStringLiteral("description"), SKText,
            QStringLiteral("给人看的备注，不影响解析"), {});
    lay->addWidget(mainBox);

    // ---- 更多属性（默认收起） ----
    auto *moreToggle = new QToolButton;
    moreToggle->setText(QStringLiteral("更多属性（位域 / 校验 / 文本词法 / 枚举）"));
    moreToggle->setCheckable(true);
    moreToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    moreToggle->setArrowType(Qt::RightArrow);
    moreToggle->setAutoRaise(true);
    lay->addWidget(moreToggle);

    auto *moreBox = new QWidget;
    auto *moreLay = new QVBoxLayout(moreBox);
    moreLay->setContentsMargins(0, 0, 0, 0);
    auto *moreRows = new QWidget;
    auto *rowsLay = new QVBoxLayout(moreRows);
    rowsLay->setContentsMargins(0, 0, 0, 0);
    rowsLay->setSpacing(4);
    moreLay->addWidget(moreRows);

    auto appendRow = [&](const SegKeyDef &d, const QJsonValue &v) {
        auto *rowW = new QWidget;
        auto *h = new QHBoxLayout(rowW);
        h->setContentsMargins(0, 0, 0, 0);
        auto *lab = new QLabel(d.label);
        lab->setFixedWidth(85);
        lab->setStyleSheet(QStringLiteral("color:#555;"));
        QWidget *editor = makeSegEditor(d.kind, d.opts);
        editor->setToolTip(d.hint);
        if (auto *le = qobject_cast<QLineEdit *>(editor))
            le->setPlaceholderText(d.hint);
        segEditorSetValue(editor, d.kind, v);
        auto *del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        del->setAutoRaise(true);
        del->setToolTip(QStringLiteral("移除这个属性"));
        h->addWidget(lab);
        h->addWidget(editor, 1);
        h->addWidget(del);
        rowsLay->addWidget(rowW);
        addBind(rowW, d.path, editor, d.kind);
        connect(del, &QToolButton::clicked, &dlg, [&binds, rowW] {
            for (int k = binds.size() - 1; k >= 0; --k)
                if (binds.at(k).row == rowW)
                    binds.removeAt(k);
            rowW->deleteLater();
        });
    };

    for (const SegKeyDef &d : segKeyDefs())
    {
        const QJsonValue v = pathGet(seg, d.path);
        if (!v.isUndefined() && !v.isNull())
            appendRow(d, v);
    }

    // 「＋ 添加属性」：只列还没加的键，选中即加一行
    auto *addCombo = new QComboBox;
    addCombo->setEditable(false);
    auto refreshAddCombo = [&] {
        addCombo->blockSignals(true);
        addCombo->clear();
        addCombo->addItem(QStringLiteral("＋ 添加属性…"));
        for (const SegKeyDef &d : segKeyDefs())
        {
            bool used = false;
            for (const Bind &b : binds)
                if (b.path == d.path)
                    used = true;
            if (!used)
                addCombo->addItem(d.label, d.path);
        }
        addCombo->setCurrentIndex(0);
        addCombo->blockSignals(false);
    };
    refreshAddCombo();
    connect(addCombo, QOverload<int>::of(&QComboBox::activated), &dlg,
            [&, addCombo](int idx) {
                if (idx <= 0)
                    return;
                const QString path = addCombo->itemData(idx).toString();
                for (const SegKeyDef &d : segKeyDefs())
                    if (d.path == path)
                    {
                        appendRow(d, QJsonValue());
                        break;
                    }
                refreshAddCombo();
            });
    auto *addBar = new QHBoxLayout;
    addBar->addWidget(addCombo);
    addBar->addStretch(1);
    moreLay->addLayout(addBar);
    moreLay->addWidget(new QLabel(QStringLiteral(
        "checks / transform 这类多条件嵌套，用下面的 JSON 入口编辑。")));
    auto *jsonBtn = new QPushButton(QStringLiteral("以 JSON 编辑整段"));
    moreLay->addWidget(jsonBtn);
    lay->addWidget(moreBox);
    moreBox->setVisible(false);
    connect(moreToggle, &QToolButton::toggled, &dlg, [&](bool on) {
        moreBox->setVisible(on);
        moreToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    lay->addStretch(1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    for (const Bind &b : binds)
        segEditorSetValue(b.editor, b.kind, pathGet(seg, b.path));

    // 模板里本来就写了非基本属性时，默认展开，省得再点一下
    if (binds.size() > 7)
        moreToggle->setChecked(true);

    // JSON 兜底：改完回填到控件
    connect(jsonBtn, &QPushButton::clicked, &dlg, [&] {
        QDialog sub(&dlg);
        sub.setWindowTitle(QStringLiteral("整段 JSON"));
        sub.resize(560, 460);
        auto *l = new QVBoxLayout(&sub);
        auto *e = new QPlainTextEdit;
        e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        e->setPlainText(QString::fromUtf8(
            QJsonDocument(seg).toJson(QJsonDocument::Indented)));
        l->addWidget(e, 1);
        auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        l->addWidget(b);
        QObject::connect(b, &QDialogButtonBox::rejected, &sub, &QDialog::reject);
        QObject::connect(b, &QDialogButtonBox::accepted, &sub, &QDialog::accept);
        if (sub.exec() != QDialog::Accepted)
            return;
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(e->toPlainText().toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !d.isObject())
            return;
        seg = d.object();
        for (const Bind &b : binds)
            segEditorSetValue(b.editor, b.kind, pathGet(seg, b.path));
    });

    // 截图钩子（仅自动化验收）
    const QString shotPath = qEnvironmentVariable("PLOTSCOPE_SHOT_SEG");
    if (!shotPath.isEmpty())
    {
        QTimer::singleShot(900, &dlg, [&dlg, shotPath] {
            dlg.grab().save(shotPath);
            dlg.accept();
        });
    }
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonObject out = seg;
    for (const Bind &b : binds)
        pathSet(out, b.path, segEditorValue(b.editor, b.kind));

    if (out.value(QStringLiteral("name")).toString().isEmpty())
        out.insert(QStringLiteral("name"), textOf(SegField));
    if (out.value(QStringLiteral("type")).toString().isEmpty())
        out.insert(QStringLiteral("type"), QStringLiteral("u8"));

    m_syncing = true;
    auto *fieldItem = table->item(row, SegField);
    fieldItem->setText(out.value(QStringLiteral("name")).toString());
    storeObjectOnItem(fieldItem, out);
    table->item(row, SegType)->setText(out.value(QStringLiteral("type")).toString());
    table->item(row, SegCount)->setText(literalText(out.value(QStringLiteral("count"))));
    table->item(row, SegValue)->setText(literalText(out.value(QStringLiteral("expect"))));
    table->item(row, SegUnit)->setText(out.value(QStringLiteral("unit")).toString());
    table->item(row, SegOther)->setText(otherKeysText(out));
    m_syncing = false;
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

// ---------------------------------------------------------------- 同步

void ProtocolEditorDialog::markDirty()
{
    if (m_syncing)
        return;
    m_dirty = true;
    m_status->setText(QStringLiteral("未落表（点校验 / 试算 / 保存或切到 JSON 页时写入）"));
}

void ProtocolEditorDialog::refreshFromJson()
{
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
    {
        m_status->setText(QStringLiteral("JSON 解析失败，协议页暂不可信：%1").arg(perr.errorString()));
        return;
    }
    const QJsonObject doc = d.object();
    m_syncing = true;

    m_name->setText(doc.value(QStringLiteral("name")).toString());
    m_desc->setText(doc.value(QStringLiteral("description")).toString());
    m_mode->setCurrentText(doc.value(QStringLiteral("mode")).toString());
    m_layout->setCurrentText(doc.value(QStringLiteral("layout")).toString());
    m_maxsize->setValue(doc.value(QStringLiteral("maxsize")).toInt(0));
    m_endian->setCurrentText(doc.value(QStringLiteral("endian")).toString());
    m_terminator->setText(doc.value(QStringLiteral("terminator")).toString());
    const QJsonObject ts = doc.value(QStringLiteral("timestamp")).toObject();
    m_tsExpr->setText(ts.value(QStringLiteral("expr")).toString());
    m_tsUnit->setCurrentText(ts.value(QStringLiteral("unit")).toString());

    {
        const QStringList opts = typeOptionsWith(doc.value(QStringLiteral("types")).toObject());
        for (QTableWidget *t : {m_segs, m_varSegs})
            if (auto *d = typeDelegateMap().value(t))
                d->setOptions(opts);
    }

    const QJsonObject series = doc.value(QStringLiteral("series")).toObject();
    m_seriesOn->setChecked(!series.isEmpty());
    m_seriesBody->setVisible(!series.isEmpty());
    m_selector->setText(series.value(QStringLiteral("selector")).toString());
    m_payload->setText(series.value(QStringLiteral("payload")).toString());
    m_fallback->setCurrentText(series.value(QStringLiteral("fallback"))
                                   .toString(QStringLiteral("reject")));

    fillSegTable(m_segs, doc.value(QStringLiteral("segments")).toArray());
    fillVariantsTable(series.value(QStringLiteral("variants")).toArray());

    m_syncing = false;
    m_dirty = false;
    if (!series.isEmpty() && m_variants->rowCount() > 0)
        m_variants->setCurrentCell(0, 0);  // 触发 reloadVariantSegs
    else
        reloadVariantSegs();
}

void ProtocolEditorDialog::commitDesign()
{
    // 只在"表被改过"时落表，避免用户正在 JSON 页手改却被控件旧值覆盖。
    if (!m_dirty)
        return;
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
        return;  // JSON 本身有语法错：留给校验去报，不偷偷重建
    m_json->setPlainText(
        QString::fromUtf8(QJsonDocument(collectDoc()).toJson(QJsonDocument::Indented)));
    m_dirty = false;
}

QJsonObject ProtocolEditorDialog::collectDoc() const
{
    QJsonParseError perr;
    QJsonObject doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr).object();
    if (perr.error != QJsonParseError::NoError)
        doc = QJsonObject{};

    doc.insert(QStringLiteral("name"), m_name->text().trimmed());
    const QString desc = m_desc->text().trimmed();
    if (desc.isEmpty())
        doc.remove(QStringLiteral("description"));
    else
        doc.insert(QStringLiteral("description"), desc);
    doc.insert(QStringLiteral("mode"), m_mode->currentText());
    doc.insert(QStringLiteral("layout"), m_layout->currentText());
    if (m_maxsize->value() > 0)
        doc.insert(QStringLiteral("maxsize"), m_maxsize->value());
    else
        doc.remove(QStringLiteral("maxsize"));
    doc.insert(QStringLiteral("endian"), m_endian->currentText());
    doc.insert(QStringLiteral("psproto"), QStringLiteral("2"));
    const QString terminator = m_terminator->text();
    if (terminator.isEmpty())
        doc.remove(QStringLiteral("terminator"));
    else
        doc.insert(QStringLiteral("terminator"), terminator);

    const QString tsExpr = m_tsExpr->text().trimmed();
    if (tsExpr.isEmpty())
    {
        doc.remove(QStringLiteral("timestamp"));
    }
    else
    {
        QJsonObject ts = doc.value(QStringLiteral("timestamp")).toObject();
        ts.insert(QStringLiteral("expr"), tsExpr);
        const QString unit = m_tsUnit->currentText().trimmed();
        if (unit.isEmpty())
            ts.remove(QStringLiteral("unit"));
        else
            ts.insert(QStringLiteral("unit"), unit);
        doc.insert(QStringLiteral("timestamp"), ts);
    }

    doc.insert(QStringLiteral("segments"), segTableToJson(m_segs));

    if (m_seriesOn->isChecked())
    {
        QJsonArray variants;
        for (int r = 0; r < m_variants->rowCount(); ++r)
        {
            QJsonObject var = objectFromItem(m_variants->item(r, 1));
            const QString name =
                m_variants->item(r, 1) ? m_variants->item(r, 1)->text().trimmed() : QString();
            if (!name.isEmpty())
                var.insert(QStringLiteral("name"), name);

            const QString matchText =
                m_variants->item(r, 0) ? m_variants->item(r, 0)->text().trimmed() : QString();
            var.remove(QStringLiteral("value"));
            var.remove(QStringLiteral("values"));
            const QStringList parts = matchText.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (parts.size() > 1)
            {
                QJsonArray vals;
                for (const QString &p : parts)
                    vals.append(parseLiteral(p));
                var.insert(QStringLiteral("values"), vals);
            }
            else if (!parts.isEmpty())
            {
                const QJsonValue v = parseLiteral(parts.first());
                if (!v.isUndefined())
                    var.insert(QStringLiteral("value"), v);
            }

            // 正在编辑的那一行，段表就是它的 segments（其余行用各自缓存）。
            if (r == m_variantRow)
                var.insert(QStringLiteral("segments"), segTableToJson(m_varSegs));
            variants.append(var);
        }
        QJsonObject series;
        series.insert(QStringLiteral("selector"), m_selector->text().trimmed());
        if (!m_payload->text().trimmed().isEmpty())
            series.insert(QStringLiteral("payload"), m_payload->text().trimmed());
        series.insert(QStringLiteral("fallback"), m_fallback->currentText());
        series.insert(QStringLiteral("variants"), variants);
        doc.insert(QStringLiteral("series"), series);
    }
    else
    {
        doc.remove(QStringLiteral("series"));
    }
    return doc;
}

// ---------------------------------------------------------------- 模板文本

QString ProtocolEditorDialog::blankTemplate()
{
    // v2 形态：mode 必填、count 是元素个数、不写已废弃的 size/role/checks。
    QJsonObject o;
    o.insert(QStringLiteral("name"), QStringLiteral("新协议"));
    o.insert(QStringLiteral("mode"), QStringLiteral("bin"));
    o.insert(QStringLiteral("layout"), QStringLiteral("sequential"));
    o.insert(QStringLiteral("endian"), QStringLiteral("little"));
    o.insert(QStringLiteral("maxsize"), 256);
    QJsonArray segs;
    auto seg = [](const QString &name, const QString &type, int count) {
        QJsonObject s;
        s.insert(QStringLiteral("name"), name);
        s.insert(QStringLiteral("type"), type);
        s.insert(QStringLiteral("count"), count);
        return s;
    };
    segs.append(seg(QStringLiteral("sync"), QStringLiteral("u8"), 1));
    segs.append(seg(QStringLiteral("a"), QStringLiteral("u16"), 1));
    segs.append(seg(QStringLiteral("b"), QStringLiteral("u16"), 1));
    o.insert(QStringLiteral("segments"), segs);
    o.insert(QStringLiteral("description"), QString());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void ProtocolEditorDialog::setTemplateText(const QString &text, const QString &path)
{
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &perr);
    m_json->setPlainText(perr.error == QJsonParseError::NoError
                             ? QString::fromUtf8(d.toJson(QJsonDocument::Indented))
                             : text);
    if (!path.isEmpty())
        m_path = path;
    refreshFromJson();
}

void ProtocolEditorDialog::setSampleFrame(const QString &hex)
{
    m_hex->setText(hex);
}

void ProtocolEditorDialog::openFirstSegmentDetail()
{
    // 自动化验收可指定行号，正常交互固定第一行
    const int row = qEnvironmentVariableIntValue("PLOTSCOPE_SHOT_SEG_ROW");
    openSegmentDetail(m_segs, row > 0 ? row : 0);
}

void ProtocolEditorDialog::selectTab(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

// ---------------------------------------------------------------- 文件动作

void ProtocolEditorDialog::newDoc()
{
    m_path.clear();
    m_json->setPlainText(blankTemplate());
    m_status->setText(QStringLiteral("新建协议（未保存）"));
    m_validation->setText(QStringLiteral("尚未校验"));
    m_preview->setRowCount(0);
    refreshFromJson();
}

void ProtocolEditorDialog::openDoc()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开协议"), nextpilot::plotscope::host::ProtocolTemplateStore::directory(),
        QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QByteArray content;
    QString err;
    // 允许 .json 草稿，但仍受大小上限约束（8 MiB）——打开动作不该把数据文件当模板吞进去。
    if (!nextpilot::plotscope::host::ProtocolTemplateStore::read(path, &content, &err, false))
    {
        QMessageBox::warning(this, QStringLiteral("打开协议"), err);
        return;
    }
    setTemplateText(QString::fromUtf8(content), path);
    m_status->setText(QStringLiteral("已打开：%1").arg(QFileInfo(path).fileName()));
    validateDoc();
}

void ProtocolEditorDialog::saveDoc()
{
    commitDesign();
    QString path = m_path;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存协议"),
            nextpilot::plotscope::host::ProtocolTemplateStore::directory() + QStringLiteral("/new.psproto"),
            QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
        if (path.isEmpty())
            return;
    }
    QString err;
    // 保存前先过校验：不能让一份编译不了的模板落盘，之后被解析器当配置读进来。
    if (!nextpilot::plotscope::host::ProtocolTemplateStore::saveToPath(
            path, m_json->toPlainText().toUtf8(), &err))
    {
        QMessageBox::warning(this, QStringLiteral("保存协议"), err);
        m_validation->setText(err);
        return;
    }
    m_path = path;
    m_status->setText(QStringLiteral("已保存：%1").arg(path));
}

void ProtocolEditorDialog::validateDoc()
{
    commitDesign();
    const auto r = runOp(QStringLiteral("protocol.validate"),
                         {{QStringLiteral("template"), m_json->toPlainText()}});
    if (!r.ok())
    {
        m_validation->setText(QStringLiteral("校验失败：%1").arg(r.error().message));
        return;
    }
    const QVariantMap p = r.value().payload.toMap();
    const QStringList issues = p.value(QStringLiteral("issues")).toStringList();
    if (p.value(QStringLiteral("ok")).toBool())
    {
        m_validation->setText(QStringLiteral("✓ 校验通过（%1 段，%2）")
                                  .arg(p.value(QStringLiteral("segmentCount")).toInt())
                                  .arg(p.value(QStringLiteral("mode")).toString()));
        return;
    }
    // 带阶段：改模板的人要先知道卡在 JSON / 结构 / 解析 / 编译哪一层。
    m_validation->setText(QStringLiteral("校验失败［%1］共 %2 项：\n%3")
                              .arg(p.value(QStringLiteral("stage")).toString())
                              .arg(issues.size())
                              .arg(issues.join(QStringLiteral("\n"))));
}

void ProtocolEditorDialog::previewFrame()
{
    commitDesign();
    m_preview->setRowCount(0);
    const QString hex = m_hex->text().trimmed();
    if (hex.isEmpty())
    {
        m_status->setText(QStringLiteral("样例帧为空"));
        return;
    }
    const auto r = runOp(QStringLiteral("protocol.preview"),
                         {{QStringLiteral("template"), m_json->toPlainText()},
                          {QStringLiteral("frame"), hex}});
    if (!r.ok())
    {
        m_status->setText(QStringLiteral("试算失败：%1").arg(r.error().message));
        return;
    }
    const QVariantMap p = r.value().payload.toMap();
    if (!p.value(QStringLiteral("ok")).toBool())
    {
        m_status->setText(QStringLiteral("试算失败：%1").arg(p.value(QStringLiteral("error")).toString()));
        return;
    }
    const QStringList lines = p.value(QStringLiteral("lines")).toStringList();
    m_preview->setRowCount(0);
    // 库输出行格式固定（"%1 @ %2  ..." / "timestamp：..." / "帧 N B · 名字"），
    // 这里按分隔符拆成三列显示；拆不开的行整体放"值"列，不丢内容。
    static const QRegularExpression segRe(
        QStringLiteral("^([^@]+?)\\s+@\\s+(\\d+)\\s{1,}(.*)$"));
    for (const QString &line : lines)
    {
        const int r = m_preview->rowCount();
        m_preview->insertRow(r);
        QString c0, c1, c2;
        const QRegularExpressionMatch m = segRe.match(line);
        if (m.hasMatch())
        {
            c0 = m.captured(1).trimmed();
            c1 = m.captured(2);
            c2 = m.captured(3).trimmed();
        }
        else if (line.contains(QStringLiteral("：")))
        {
            const int idx = line.indexOf(QStringLiteral("："));
            c0 = line.left(idx).trimmed();
            c2 = line.mid(idx + 1).trimmed();
        }
        else
        {
            const int dot = line.indexOf(QStringLiteral(" · "));
            if (dot > 0)
            {
                c0 = line.left(dot).trimmed();
                c2 = line.mid(dot + 3).trimmed();
            }
            else
            {
                c2 = line;
            }
        }
        auto *i0 = new QTableWidgetItem(c0);
        auto *i1 = new QTableWidgetItem(c2);
        // 值列常有整段 raw 数组，放不下时悬停看全文。
        i1->setToolTip(c2);
        i0->setToolTip(line);
        m_preview->setItem(r, 0, i0);
        m_preview->setItem(r, 1, i1);
    }
    m_status->setText(QStringLiteral("试算完成：%1 项（%2 B）")
                          .arg(lines.size())
                          .arg(p.value(QStringLiteral("frameBytes")).toInt()));
}

void ProtocolEditorDialog::applyTheme()
{
    // 走全局 QSS/palette（ThemeManager 已设），不写死色值。
    setStyleSheet(QString());
}

void ProtocolEditorDialog::configureChecksumTransform()
{
    if (!m_appCtx)
    {
        QMessageBox::information(this, QStringLiteral("Checksum 变换"),
                                 QStringLiteral("当前没有应用上下文，无法调用在线调试。"));
        return;
    }
    // 按 CapChecksum 过滤，用户只能选适用于 checksum 的算子/表达式/Lua。
    const QJsonObject req = TransformEditorDialog::run(
        this, *m_appCtx, CapChecksum, QStringLiteral("协议 Checksum"));
    if (req.isEmpty())
        return;
    m_checksumRequest = req;
    const QString id = req.value(QStringLiteral("descriptorId")).toString();
    m_checksumLabel = TransformRegistry::get(id).label;
    m_status->setText(QStringLiteral("Checksum 变换已配置：%1").arg(m_checksumLabel));
}

void ProtocolEditorDialog::run(QWidget *parent, AppContext *appCtx,
                               nextpilot::plotscope::core::IIntentExecutor *executor)
{
    ProtocolEditorDialog dlg(parent, appCtx, executor);
    dlg.exec();
}

nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult>
ProtocolEditorDialog::runOp(const QString &op, const QVariantMap &params)
{
    nextpilot::plotscope::core::Intent intent;
    intent.op = op;
    intent.params = params;
    if (m_executor)
        return m_executor->execute(intent);
    // 没有宿主执行器（独立打开/截图场景）：用同一模块的 handler 直连，
    // 逻辑与模块注册的那份完全相同，不复制任何校验规则。
    static nextpilot::plotscope::host::ProtocolIntentHandler local;
    return local.execute(intent);
}

} // namespace nextpilot::plotscope::gui
