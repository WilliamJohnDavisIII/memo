// memo — record voice memos with one key.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

static const char *kVersion = "0.1.0";

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // Commands for scripts and agents run without a display.
    if (isCliCommand(command)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("memo");
        app.setApplicationVersion(kVersion);
        QStringList arguments = app.arguments();
        arguments.removeFirst();
        return runCli(arguments);
    }
    if (!command.isEmpty() && command != "open") {
        QCoreApplication app(argc, argv);
        return runCli({command});
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("memo");
    app.setApplicationVersion(kVersion);

    // Matches the window to memo.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("memo");
    app.setWindowIcon(QIcon::fromTheme("memo"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
