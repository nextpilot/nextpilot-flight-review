#include "PersistentStateHub.h"

#include <QJsonObject>
#include <QStringList>

#include <cstdio>
#include <stdexcept>

/*
 * PersistentStateHub 验收：
 *   - 收集后的 JSON 能原样恢复回条目状态（往返）
 *   - Session / Document 分段落格，互不串段
 *   - 登记序即恢复序
 *   - bestEffort 条目抛异常被吞掉，后续条目照常恢复
 */
static int g_fail = 0;
#define OK(cond, msg)                                                              \
    do                                                                             \
    {                                                                              \
        if (cond)                                                                  \
            std::printf("[ok] %s\n", msg);                                          \
        else                                                                       \
        {                                                                          \
            std::printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);           \
            ++g_fail;                                                              \
        }                                                                          \
    } while (0)

using nextpilot::plotscope::context::PersistentStateEntry;
using nextpilot::plotscope::context::PersistentStateHub;

static PersistentStateEntry makeEntry(const QString &id, PersistentStateEntry::Scope scope,
                                      const QString &field, QString &backing,
                                      QStringList &order)
{
    return PersistentStateEntry{
        id,
        scope,
        1,
        [&backing, field]() { return QJsonObject{{field, backing}}; },
        [&backing, field, id, &order](const QJsonObject &obj) {
            backing = obj.value(field).toString();
            order.append(id);
        },
        true};
}

int main()
{
    // 往返：collect 产物喂回 restore，字段值一致。
    {
        PersistentStateHub hub;
        QString document = QStringLiteral("draft-page");
        QString session = QStringLiteral("active-cell-2");
        QStringList order;

        hub.registerEntry(makeEntry(QStringLiteral("plot.document"), PersistentStateEntry::Document,
                                    QStringLiteral("value"), document, order));
        hub.registerEntry(makeEntry(QStringLiteral("canvas.session"), PersistentStateEntry::Session,
                                    QStringLiteral("value"), session, order));

        const QJsonObject snapshot = hub.collect();
        OK(snapshot.value(QStringLiteral("document"))
                   .toObject()
                   .contains(QStringLiteral("plot.document")),
           "document 段包含 plot.document");
        OK(snapshot.value(QStringLiteral("session"))
                   .toObject()
                   .contains(QStringLiteral("canvas.session")),
           "session 段包含 canvas.session");
        OK(!snapshot.value(QStringLiteral("session"))
                    .toObject()
                    .contains(QStringLiteral("plot.document")),
           "两段互不串段");

        document.clear();
        session.clear();
        hub.restore(snapshot);
        OK(document == QStringLiteral("draft-page"), "document 条目恢复原值");
        OK(session == QStringLiteral("active-cell-2"), "session 条目恢复原值");
        OK(order.size() == 2 && order.at(0) == QStringLiteral("plot.document") &&
               order.at(1) == QStringLiteral("canvas.session"),
           "登记序即恢复序");
    }

    // bestEffort：抛异常的条目被吞掉，后面的条目继续恢复。
    {
        PersistentStateHub hub;
        QStringList order;
        QString tail = QStringLiteral("untouched");

        hub.registerEntry(PersistentStateEntry{
            QStringLiteral("broken"),
            PersistentStateEntry::Session,
            1,
            []() { return QJsonObject{{QStringLiteral("v"), 1}}; },
            [&order](const QJsonObject &) {
                order.append(QStringLiteral("broken"));
                throw std::runtime_error("boom");
            },
            true});
        hub.registerEntry(PersistentStateEntry{
            QStringLiteral("intact"),
            PersistentStateEntry::Session,
            1,
            []() { return QJsonObject{{QStringLiteral("v"), 2}}; },
            [&order, &tail](const QJsonObject &obj) {
                tail = QString::number(obj.value(QStringLiteral("v")).toInt());
                order.append(QStringLiteral("intact"));
            },
            true});

        hub.restore(hub.collect());
        OK(order.size() == 2, "异常条目未阻断后续恢复");
        OK(tail == QStringLiteral("2"), "后续条目值正确");
    }

    // 缺段恢复：快照里没有该条目时不调用 restore。
    {
        PersistentStateHub hub;
        int calls = 0;
        hub.registerEntry(PersistentStateEntry{
            QStringLiteral("only-doc"),
            PersistentStateEntry::Document,
            1,
            []() { return QJsonObject{}; },
            [&calls](const QJsonObject &) { ++calls; },
            true});
        hub.restore(QJsonObject{{QStringLiteral("session"), QJsonObject{}}});
        OK(calls == 0, "缺失的条目跳过恢复");
    }

    std::printf(g_fail == 0 ? "全部通过\n" : "失败 %d 项\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
