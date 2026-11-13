#pragma once

#include <QStringList>

// Commands that need no display, so scripts and agents can find memos.
bool isCliCommand(const QString &command);
int runCli(const QStringList &arguments);
