#include "VideoComponent.h"

#include "VideoSyncWidget.h"

#include <QDockWidget>

namespace ps::components
{
namespace
{
struct VideoRegistrar
{
    VideoRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("video.sync"), QStringLiteral("视频同步"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<VideoComponent>(); });
    }
};
static VideoRegistrar g_videoRegistrar;
} // namespace

ComponentManifest VideoComponent::manifest() const
{
    return {QStringLiteral("video.sync"), QStringLiteral("视频同步"), QStringLiteral("1.0"),
            {QStringLiteral("dock")}};
}

void VideoComponent::contribute(Registry &registry, const HostContext &host)
{
    registry.addDock(DockContribution{
        QStringLiteral("dock.video"), QStringLiteral("视频同步"),
        Qt::RightDockWidgetArea, false, 3, QStringLiteral(":/icons/box.svg"),
        [host](QWidget *parent) -> QWidget * {
            auto *dock = new QDockWidget(QStringLiteral("视频同步"), parent);
            dock->setObjectName(QStringLiteral("dock.video"));
            auto *video = new VideoSyncWidget(dock);
            dock->setWidget(video);
            if (host.setPlayhead)
                QObject::connect(video, &VideoSyncWidget::timeChanged, dock,
                                 [setPlayhead = host.setPlayhead](double t) { setPlayhead(t); });
            return dock;
        }});
}

} // namespace ps::components
