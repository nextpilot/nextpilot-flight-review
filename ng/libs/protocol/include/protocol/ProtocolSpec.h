#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "protocol/ProtocolDocument.h"

#include <string>

namespace ps::protocol
{

/// 顺序段协议的结构校验。app 编辑器与 loader 共用同一套规则，避免口径漂移。
QStringList validate(const QJsonObject &root);

QStringList typeNames();
bool parseType(const std::string &raw, TypeInfo &ti, std::string &err);
int typeBytes(const QString &type);
bool literalSize(const QJsonValue &value, int *out);
int operatorWidth(const QString &op);

} // namespace ps::protocol
