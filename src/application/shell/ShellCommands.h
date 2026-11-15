#pragma once

class MainWindow;

namespace shell
{

void registerCommands(MainWindow &win);
void bindSession(MainWindow &win);

} // namespace shell
