#pragma once

#include "Component.h"

class QWidget;

namespace ps::components
{
void registerAutopilotReview(Registry &registry, HostContext &host);
QWidget *createFlightReviewApp(QWidget *parent, const QString &path);
}
