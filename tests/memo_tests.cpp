#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "theme.h"
#include "translator.h"

class MemoTests : public QObject {
    Q_OBJECT

private slots:
    void namesFollowTheStartTime()
    {
        QTemporaryDir dir;
        const QDateTime when(QDate(2026, 11, 12), QTime(18, 58, 3));
        const QString first = Backend::nextPath(dir.path(), when, "ogg");
        QCOMPARE(QFileInfo(first).fileName(), QString("2026-11-12 18-58-03.ogg"));

        write(first, "x");
        const QString second = Backend::nextPath(dir.path(), when, "ogg");
        QCOMPARE(QFileInfo(second).fileName(), QString("2026-11-12 18-58-03 (2).ogg"));
        QCOMPARE(Backend::createdAt(QFileInfo(second)), when);
    }

    void scanListsAudioNewestFirst()
    {
        QTemporaryDir dir;
        write(dir.filePath("2026-11-10 09-00-00.ogg"), "x");
        write(dir.filePath("2026-11-12 09-00-00.ogg"), "x");
        write(dir.filePath("2026-11-11 09-00-00.m4a"), "x");
        write(dir.filePath("notes.txt"), "x");
        write(dir.filePath(".recording.ogg"), "");

        const auto memos = Backend::scan(dir.path());
        QCOMPARE(memos.size(), 3);
        QCOMPARE(QFileInfo(memos[0].path).fileName(), QString("2026-11-12 09-00-00.ogg"));
        QCOMPARE(QFileInfo(memos[2].path).fileName(), QString("2026-11-10 09-00-00.ogg"));
    }

    void unfinishedRecordingsAreKept()
    {
        QTemporaryDir dir;
        write(dir.filePath(".recording.ogg"), "audio");
        Backend backend(dir.path());
        QCOMPARE(backend.memos().size(), 1);
        QVERIFY(!QFileInfo::exists(dir.filePath(".recording.ogg")));
    }

    void trashUndoAndRedo()
    {
        // The desktop trash only takes files on the home volume, so test there.
        QTemporaryDir dir(QDir::homePath() + "/.cache/memo-test-XXXXXX");
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("2026-11-12 09-00-00.ogg");
        write(path, "audio");

        Backend backend(dir.path());
        QCOMPARE(backend.memos().size(), 1);

        QVERIFY(backend.remove(path));
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(backend.memos().size(), 0);
        QVERIFY(backend.canUndo());

        backend.undo();
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(backend.memos().size(), 1);
        QVERIFY(backend.canRedo());

        backend.redo();
        QVERIFY(!QFileInfo::exists(path));
        backend.undo();
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(backend.error().isEmpty());
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Today").isNull());
    }

    void shippedCatalogsCoverEveryString()
    {
        // Every language ships the same keys as Danish, the reference catalog.
        const QDir catalogs(QFINDTESTDATA("../src/i18n"));
        const auto reference = keys(catalogs.filePath("da.json"));
        QVERIFY(!reference.isEmpty());
        for (const auto &file : catalogs.entryList({"*.json"})) {
            if (file == "en.json")
                continue;
            QCOMPARE(keys(catalogs.filePath(file)), reference);
        }
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "da\n");
        Translator translator(dir.path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.translate("Main", "Today"), QString("I dag"));
        QCOMPARE(translator.translate("Main", "%n memos", nullptr, 1), QString("%n memo"));
        QCOMPARE(translator.translate("Main", "%n memos", nullptr, 3), QString("%n memoer"));
    }

private:
    static QStringList keys(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        auto list = QJsonDocument::fromJson(file.readAll()).object().keys();
        list.sort();
        return list;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(MemoTests)
#include "memo_tests.moc"
