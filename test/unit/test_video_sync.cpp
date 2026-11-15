#include "VideoSyncWidget.h"

#include <QApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
    {
        std::printf("ffmpeg not found, skip\n");
        return 0;
    }
    QTemporaryDir dir;
    const QString video = dir.path() + QStringLiteral("/clip.mp4");
    QProcess make;
    make.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("testsrc=duration=1:size=160x120:rate=10"),
                        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video});
    if (!make.waitForStarted(5000) || !make.waitForFinished(30000))
    {
        std::printf("fixture generation failed\n");
        return 1;
    }

    VideoSyncWidget w;
    if (!w.loadFile(video))
    {
        std::printf("loadFile failed\n");
        return 1;
    }
    if (!w.isLoaded() || w.duration() < 0.5)
    {
        std::printf("unexpected duration %.2f\n", w.duration());
        return 1;
    }
    return 0;
}
