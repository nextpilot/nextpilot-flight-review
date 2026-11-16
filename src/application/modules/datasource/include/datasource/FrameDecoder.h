#pragma once

#include "DecodedFrame.h"
#include "ProtocolCatalog.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class FrameDecoder
{
public:
    static bool decode(const PsMessageDescriptor &descriptor, const QByteArray &data, int offset,
                       QVector<PsDecodedField> &fields, QString *error = nullptr);
    static bool decodeText(const PsMessageDescriptor &descriptor, const QStringList &tokens,
                           QVector<PsDecodedField> &fields);
    static int requiredSize(const PsMessageDescriptor &descriptor);
};
