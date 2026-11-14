#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace ps::protocol
{

/// 顺序段协议的结构校验。app 编辑器与 loader 共用同一套规则，避免口径漂移。
QStringList validate(const QJsonObject &root);

int typeBytes(const QString &type);
bool literalSize(const QJsonValue &value, int *out);
int operatorWidth(const QString &op);

} // namespace ps::protocol
