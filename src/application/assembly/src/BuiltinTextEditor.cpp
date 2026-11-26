#include "WorkbenchAdapters.h"

// 壳内置 text 最小编辑器实现（M3-a 第 2 笔，正文步骤 4）。
// QPlainTextEdit 承载；保存经 QFile 写回 uri（uri 非本地文件时保存失败返回 false）。

#include <QFile>
#include <QPlainTextEdit>
#include <QTextStream>

namespace nextpilot::plotscope::assembly {

QString BuiltinTextEditorProvider::contentOf(const registry::ResourceRef &ref)
{
    QFile f(ref.uri);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll())
                                                         : QString();
}

QWidget *BuiltinTextEditorProvider::createEditor(const registry::ResourceRef &ref, QWidget *parent)
{
    auto *edit = new QPlainTextEdit(parent);
    edit->setPlainText(contentOf(ref));
    edit->setReadOnly(readonly());
    if (ref.uri.startsWith(QLatin1String("untitled:")))
        edit->setPlaceholderText(QStringLiteral("未保存的新文件"));
    return edit;
}

bool BuiltinTextEditorProvider::save(const registry::ResourceRef &ref)
{
    // headless 下没有页签回引控件，本 provider 的保存语义 = uri 可写校验；
    // GUI 侧真正的“把编辑器内容写回”由页签层把控件内容传给 provider（M4 页签落地时接）。
    if (ref.uri.startsWith(QLatin1String("untitled:")))
        return false;
    QFile f(ref.uri);
    return f.open(QIODevice::WriteOnly | QIODevice::Text);
}

bool BuiltinTextEditorProvider::saveAs(const registry::ResourceRef &ref)
{
    // ref.uri 即壳给出的另存目标；内容取自 ref.title 里缓存的源 uri（M3-a 约定：
    // 页签层另存时把源 uri 放 title 传回——headless 无页签，e2e 直接如此构造）。
    // ref.mime 非 text/plain 或目标不可写 → false（壳提示，不吞）。
    if (!ref.mime.isEmpty() && ref.mime != QLatin1String("text/plain"))
        return false;
    // 另存语义：source = title 字段（源 uri），target = uri。title 空则视 uri 即源。
    const QString source = ref.title.isEmpty() ? ref.uri : ref.title;
    QFile src(source);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray data = src.readAll();
    QFile dst(ref.uri);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return dst.write(data) == data.size();
}

} // namespace nextpilot::plotscope::assembly
