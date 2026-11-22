#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include "protocol/ProtocolDocument.h"

#include <string>

namespace nextpilot::plotscope::protocol
{

/// 形态无关的校验中间态：bin/text 各自填充段表，公共部分（series/types/timestamp/term）共用。
struct SpecContext
{
    QStringList errs;
    QSet<QString> names;
    QMap<QString, int> order;
    QMap<QString, int> fixedBytes; // 段名 → 固定字节数（变长段不登记）
    QMap<QString, QString> segTypes; // 段名 → 类型名（series.payload 之类只看类型，不看字节数）
    QString payloadName;
    int maxsize = 4096;
    bool hasVariable = false;
    bool hasWildcard = false; // count:"*"
};

/// 顺序段协议的结构校验。app 编辑器与 loader 共用同一套规则，避免口径漂移。
/// 唯一入口：按顶层 mode 分派到 validateBin / validateText。
QStringList validate(const QJsonObject &root);

/// bin 形态：字节/位域段，10 键（name/description/unit/output/type/count/transform/expect/on_mismatch/if）。
QStringList validateBin(const QJsonObject &root);

/// text 形态：词法切分段（key/literal/delimiter/width/base/fixed/signed/sci/pattern/empty/rest/repeater）。
QStringList validateText(const QJsonObject &root);

/// 两种形态共用的顶层校验：series/types/timestamp/term/maxsize 与固定总字节。
void validateShared(const QJsonObject &root, SpecContext &ctx);

/// 表达式里出现的标识符（词法扫描，不解析语法），供"引用必须已定义"校验。
QStringList exprIdentifiers(const QString &expr);

/// 校验表达式里的标识符：只允许保留字、前序段名、派生名；其余报未知引用。
void checkExprRefs(const QString &expr, const QString &owner, const SpecContext &ctx, int selfIndex,
                   QStringList &errs);

QStringList typeNames();
bool parseType(const std::string &raw, TypeInfo &ti, std::string &err);
int typeBytes(const QString &type);
bool literalSize(const QJsonValue &value, int *out);
int operatorWidth(const QString &op);

} // namespace nextpilot::plotscope::protocol
