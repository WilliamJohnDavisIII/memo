#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "transcriber.h"
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
        QTemporaryDir dir(homeTemplate());
        QVERIFY(dir.isValid());
        if (!trashWorks(dir.path()))
            QSKIP("No desktop trash in this environment");
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

    void levelsSitOnADecibelScale()
    {
        QCOMPARE(Backend::levelFor(0.0f), 0.0);
        QCOMPARE(Backend::levelFor(1.0f), 1.0);
        const qreal speech = Backend::levelFor(0.1f);   // -20 dB
        const qreal hiss = Backend::levelFor(0.003f);   // about -50 dB
        QVERIFY(speech > 0.6 && speech < 0.8);
        QVERIFY(hiss < 0.1);
    }

    void levelsStartFlat()
    {
        QTemporaryDir dir;
        Backend backend(dir.path());
        QCOMPARE(backend.levels().size(), Backend::kBars);
        QCOMPARE(backend.levels().value(0), 0.0);
    }

    void transcribeWritesTheTranscriptBeside()
    {
        QTemporaryDir dir;
        const QString fake = fakeSuperwhisper(dir.path(), offlineReady(true),
                                              R"({"session_id":7,"text":"Pick up milk."})");
        write(dir.filePath("busy"), "");  // the first submit is turned away, then retried
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");

        Backend backend(dir.path(), nullptr, fake);
        backend.transcriber()->setPollInterval(5);
        backend.transcribe(memo);
        QTRY_COMPARE(Backend::readTranscript(memo), QString("Pick up milk."));
        QTRY_COMPARE(backend.memos().value(0).toMap().value("transcript").toString(), QString("Pick up milk."));
        QVERIFY(backend.error().isEmpty());
        QVERIFY(QFileInfo::exists(dir.filePath("2026-11-12 09-00-00.txt")));
    }

    void missingSuperwhisperSaysSo()
    {
        QTemporaryDir dir;
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");
        Backend backend(dir.path(), nullptr, dir.filePath("no-such-superwhisper"));
        backend.transcribe(memo);
        QTRY_VERIFY(backend.error().contains("needs Superwhisper"));
        QVERIFY(!QFileInfo::exists(Backend::transcriptPath(memo)));
    }

    void missingModelSaysSo()
    {
        QTemporaryDir dir;
        const QString fake = fakeSuperwhisper(dir.path(), offlineReady(false), "{}");
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");
        Backend backend(dir.path(), nullptr, fake);
        backend.transcribe(memo);
        QTRY_VERIFY(backend.error().contains("isn't downloaded"));
        QVERIFY(!QFileInfo::exists(dir.filePath("submitted")));
    }

    void cloudModeAsksBeforeUploading()
    {
        QTemporaryDir dir;
        const QString fake = fakeSuperwhisper(dir.path(),
            R"({"processing":"cloud","credentials":{"available":true},"selectionModels":[]})",
            R"({"session_id":7,"text":"From the cloud."})");
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");

        Backend backend(dir.path(), nullptr, fake);
        backend.transcriber()->setPollInterval(5);
        QSignalSpy asked(&backend, &Backend::cloudConsentNeeded);
        backend.transcribe(memo);
        QTRY_COMPARE(asked.count(), 1);
        QVERIFY(!QFileInfo::exists(dir.filePath("submitted")));

        backend.transcribe(memo, true);
        QTRY_COMPARE(Backend::readTranscript(memo), QString("From the cloud."));
    }

    void silentMemoSaysNoSpeech()
    {
        QTemporaryDir dir;
        const QString fake = fakeSuperwhisper(dir.path(), offlineReady(true),
                                              R"({"session_id":7,"text":"","warning":"No speech detected"})");
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");
        Backend backend(dir.path(), nullptr, fake);
        backend.transcriber()->setPollInterval(5);
        backend.transcribe(memo);
        QTRY_COMPARE(backend.error(), QString("No speech found in this memo."));
        QVERIFY(!QFileInfo::exists(Backend::transcriptPath(memo)));
    }

    void trashTakesTheTranscriptAlong()
    {
        QTemporaryDir dir(homeTemplate());
        QVERIFY(dir.isValid());
        if (!trashWorks(dir.path()))
            QSKIP("No desktop trash in this environment");
        const QString memo = dir.filePath("2026-11-12 09-00-00.ogg");
        write(memo, "audio");
        QVERIFY(Backend::writeTranscript(memo, "Hello"));

        Backend backend(dir.path());
        QVERIFY(backend.remove(memo));
        QVERIFY(!QFileInfo::exists(Backend::transcriptPath(memo)));
        backend.undo();
        QVERIFY(QFileInfo::exists(memo));
        QCOMPARE(Backend::readTranscript(memo), QString("Hello"));
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
    // Some containers have no usable desktop trash (Qt can't place the home
    // volume), so trash behavior can only be tested where the trash works.
    static bool trashWorks(const QString &dir)
    {
        const QString probe = dir + "/.trash-probe";
        write(probe, "x");
        QString inTrash;
        if (!QFile::moveToTrash(probe, &inTrash))
            return false;
        QFile::remove(inTrash);
        const QFileInfo trashed(inTrash);
        QFile::remove(trashed.dir().absolutePath() + "/../info/" + trashed.fileName() + ".trashinfo");
        return true;
    }

    // A fresh account (like CI's) may have no ~/.cache yet.
    static QString homeTemplate()
    {
        const QString cache = QDir::homePath() + "/.cache";
        QDir().mkpath(cache);
        return cache + "/memo-test-XXXXXX";
    }

    static QByteArray offlineReady(bool ready)
    {
        const QByteArray r = ready ? "true" : "false";
        return R"({"processing":"offline_lite","selectionModels":[{"id":"transcription","ready":)" + r + "}]}";
    }

    // Stands in for Superwhisper's CLI: answers from files beside it and
    // records what was submitted.
    static QString fakeSuperwhisper(const QString &dir, const QByteArray &models, const QByteArray &read)
    {
        write(dir + "/models.json", models);
        write(dir + "/read.json", read);
        const QString script = dir + "/superwhisper";
        write(script, "#!/bin/sh\n"
                      "D=$(dirname \"$0\")\n"
                      "case \"$1\" in\n"
                      "  models) cat \"$D/models.json\" ;;\n"
                      "  transcribe) if [ -f \"$D/busy\" ]; then rm \"$D/busy\"; echo '{\"error\":\"Not ready: transcribing\"}'; exit 1; fi\n"
                      "    echo \"$2\" >> \"$D/submitted\"; echo '{\"ok\":true,\"session_id\":7}' ;;\n"
                      "  read) cat \"$D/read.json\" ;;\n"
                      "esac\n");
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return script;
    }

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
