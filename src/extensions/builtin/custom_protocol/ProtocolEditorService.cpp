#include "ProtocolEditorService.h"

namespace ps::components
{
namespace
{
ProtocolEditorFactory &factory()
{
    static ProtocolEditorFactory f;
    return f;
}
} // namespace

void setProtocolEditorFactory(ProtocolEditorFactory f)
{
    factory() = std::move(f);
}

QDialog *createProtocolEditor(const QString &path, QWidget *parent)
{
    return factory() ? factory()(path, parent) : nullptr;
}

bool hasProtocolEditor()
{
    return static_cast<bool>(factory());
}

} // namespace ps::components
