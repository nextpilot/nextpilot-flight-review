#pragma once

#include <functional>
#include <QString>

class QDialog;
class QWidget;

namespace ps::components
{

using ProtocolEditorFactory = std::function<QDialog *(const QString &, QWidget *)>;

void setProtocolEditorFactory(ProtocolEditorFactory factory);
QDialog *createProtocolEditor(const QString &path, QWidget *parent);
bool hasProtocolEditor();

} // namespace ps::components
