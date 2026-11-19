#pragma once

#include "Intent.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QJsonObject;

namespace nextpilot::plotscope::host
{

// 协议模块的意图面（headless 与 GUI 共用一条路径）：
//
//   protocol.validate         模板 JSON/文件 → { ok, issues[] }        （编辑器"校验"的同一份内核）
//   protocol.preview          模板 + hex 单帧 → { ok, lines[], error }   （编辑器"试算单帧"的同一份内核）
//   protocol.templates.list   列举模板库 → { dir, items[] }
//   protocol.templates.save   写模板（先校验后落盘）→ { ok, path }
//
// 为什么走意图而不是模块私有 API：
//   脚本、CLI、GUI 按钮已经在用同一套 Intent 分发（data.load / plot.render 皆如此）。
//   协议能力如果只挂在 GUI 的对话框里，headless 场景就得复制一份校验逻辑——
//   那正是"库搬完了、能力还锁在界面里"的老问题。
//
// 边界：不碰 Widgets、不碰 Dataset、不改协议库；只做参数整理与结果整形。
class ProtocolIntentHandler final : public QObject, public nextpilot::plotscope::core::IIntentHandler
{
    Q_OBJECT
public:
    explicit ProtocolIntentHandler(QObject *parent = nullptr);

    bool canHandle(const nextpilot::plotscope::core::Intent &intent) const override;
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> execute(
        const nextpilot::plotscope::core::Intent &intent) override;

    // 供测试直连：不经过 RuntimeHost 也能跑同一份逻辑。
    static QStringList supportedOps();

private:
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> validateOp(
        const QVariantMap &params);
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> previewOp(
        const QVariantMap &params);
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> listOp(
        const QVariantMap &params);
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> saveOp(
        const QVariantMap &params);

    // 取模板文本：优先 params["template"]（内联），否则 params["templatePath"]（文件）。
    bool loadTemplate(const QVariantMap &params, QByteArray *out, QString *err) const;
};

} // namespace nextpilot::plotscope::host
