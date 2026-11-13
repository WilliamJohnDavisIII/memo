#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

// Turns memos into text through Superwhisper's command line, one at a time.
// Superwhisper does the listening; this only queues files, checks that
// Superwhisper is ready, and waits for its answer.
class Transcriber : public QObject {
    Q_OBJECT

public:
    explicit Transcriber(const QString &program = "superwhisper", QObject *parent = nullptr);

    // allowCloud says the user agreed to upload audio when Superwhisper is in
    // Cloud mode; without it, Cloud mode asks first via cloudConsentNeeded.
    void transcribe(const QString &audioPath, bool allowCloud = false);
    bool isBusy(const QString &audioPath) const;
    QStringList busyPaths() const;

    // Shorter waits make the tests quick.
    void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }

signals:
    void started(const QString &audioPath);
    void finished(const QString &audioPath, const QString &text);
    void failed(const QString &audioPath, const QString &message);
    void cloudConsentNeeded(const QString &audioPath);

private:
    enum class Step { Idle, CheckModels, Submit, Read };

    void next();
    void run(Step step, const QStringList &arguments);
    void handle(const QJsonObject &reply, const QString &rawError);
    void finishCurrent();
    void fail(const QString &message);

    QString m_program;
    QStringList m_queue;
    QString m_current;
    bool m_currentAllowsCloud = false;
    QStringList m_cloudAllowed;
    Step m_step = Step::Idle;
    qint64 m_session = -1;
    int m_attempts = 0;
    QProcess m_process;
    QTimer m_poll;
};
