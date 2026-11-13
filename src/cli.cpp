#include "cli.h"

#include "backend.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {

const char *kUsage =
    "Usage: memo [command]\n"
    "\n"
    "  memo                 open the recorder\n"
    "  memo open            open the recorder\n"
    "  memo list [--json]   list memos, newest first, with durations\n"
    "  memo dir             print the memos folder\n"
    "  memo --version       print the version\n";

QString clock(qint64 ms) {
    if (ms < 0)
        return "-:--";
    const qint64 seconds = ms / 1000;
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

} // namespace

bool isCliCommand(const QString &command) {
    return QStringList{"list", "dir", "help", "-h", "--help", "--version"}.contains(command);
}

int runCli(const QStringList &arguments) {
    QTextStream out(stdout), err(stderr);
    const QString command = arguments.value(0);

    if (command == "help" || command == "-h" || command == "--help") {
        out << kUsage;
        return 0;
    }
    if (command == "--version") {
        out << "memo " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }
    if (command == "dir") {
        out << Backend::defaultDirectory() << "\n";
        return 0;
    }
    if (command == "list") {
        const bool json = arguments.contains("--json");
        auto memos = Backend::scan(Backend::defaultDirectory());
        for (auto &memo : memos)
            memo.duration = Backend::probeDuration(memo.path);

        if (json) {
            QJsonArray array;
            for (const auto &memo : memos)
                array.append(QJsonObject{{"path", memo.path},
                                         {"created", memo.created.toString(Qt::ISODate)},
                                         {"duration_ms", memo.duration},
                                         {"transcript", memo.transcript}});
            out << QJsonDocument(array).toJson(QJsonDocument::Indented);
        } else {
            for (const auto &memo : memos)
                out << memo.created.toString("yyyy-MM-dd HH:mm") << "  " << clock(memo.duration).rightJustified(6)
                    << "  " << memo.path << "\n";
        }
        return 0;
    }

    err << "memo: unknown command '" << command << "'\n\n" << kUsage;
    return 1;
}
