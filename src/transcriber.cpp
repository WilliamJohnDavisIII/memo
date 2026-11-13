#include "transcriber.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

// Superwhisper takes one transcription at a time and says "Not ready" while
// it's busy (with a dictation, say). Waiting this many polls covers a long
// memo without hanging forever if it never answers.
constexpr int kMaxPolls = 2400;

} // namespace

Transcriber::Transcriber(const QString &program, QObject *parent) : QObject(parent), m_program(program) {
    m_poll.setSingleShot(true);
    m_poll.setInterval(750);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (m_step == Step::Submit)
            run(Step::Submit, {"transcribe", m_current});
        else if (m_step == Step::Read)
            run(Step::Read, {"read"});
    });

    connect(&m_process, &QProcess::finished, this, [this] {
        const QByteArray out = m_process.readAllStandardOutput();
        const QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        handle(QJsonDocument::fromJson(out).object(), err);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Transcription needs Superwhisper. Install it from the Omarchy menu, "
                    "or run omarchy-install-dictation-superwhisper."));
    });
}

void Transcriber::transcribe(const QString &audioPath, bool allowCloud) {
    if (allowCloud && !m_cloudAllowed.contains(audioPath))
        m_cloudAllowed.append(audioPath);
    if (isBusy(audioPath))
        return;
    m_queue.append(audioPath);
    emit started(audioPath);
    next();
}

bool Transcriber::isBusy(const QString &audioPath) const {
    return m_current == audioPath || m_queue.contains(audioPath);
}

QStringList Transcriber::busyPaths() const {
    QStringList paths = m_queue;
    if (!m_current.isEmpty())
        paths.prepend(m_current);
    return paths;
}

void Transcriber::next() {
    if (m_step != Step::Idle || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    m_attempts = 0;
    m_session = -1;

    if (!QFileInfo(m_program).isExecutable() && QStandardPaths::findExecutable(m_program).isEmpty()) {
        fail(tr("Transcription needs Superwhisper. Install it from the Omarchy menu, "
                "or run omarchy-install-dictation-superwhisper."));
        return;
    }
    run(Step::CheckModels, {"models", "show"});
}

void Transcriber::run(Step step, const QStringList &arguments) {
    m_step = step;
    m_process.start(m_program, arguments);
}

void Transcriber::handle(const QJsonObject &reply, const QString &rawError) {
    const QString error = reply.value("error").toString(rawError);

    switch (m_step) {
    case Step::Idle:
        return;

    // Check the mode before sending anything: an offline mode needs its
    // models downloaded, and Cloud mode uploads the audio, so it asks first.
    case Step::CheckModels: {
        const QString mode = reply.value("processing").toString();
        if (mode.isEmpty()) {
            fail(tr("Superwhisper isn't set up yet. Open Superwhisper to finish setting it up."));
            return;
        }
        if (mode.startsWith("cloud")) {
            if (!reply.value("credentials").toObject().value("available").toBool()) {
                fail(tr("Superwhisper's Cloud mode isn't signed in. Open Superwhisper to set it up."));
                return;
            }
            if (!m_cloudAllowed.contains(m_current)) {
                const QString path = m_current;
                finishCurrent();
                emit cloudConsentNeeded(path);
                return;
            }
        } else {
            const auto models = reply.value("selectionModels").toArray();
            for (const auto &model : models) {
                if (!model.toObject().value("ready").toBool()) {
                    fail(tr("Superwhisper's speech model isn't downloaded yet. "
                            "Run superwhisper models download, or open Superwhisper to set it up."));
                    return;
                }
            }
        }
        run(Step::Submit, {"transcribe", m_current});
        return;
    }

    case Step::Submit:
        if (reply.contains("session_id")) {
            m_session = reply.value("session_id").toInteger(-1);
            m_attempts = 0;
            m_step = Step::Read;
            m_poll.start();
        } else if (error.contains("Not ready", Qt::CaseInsensitive) && ++m_attempts < kMaxPolls) {
            m_poll.start();
        } else {
            fail(tr("Superwhisper couldn't transcribe this memo: %1").arg(error.isEmpty() ? tr("no answer") : error));
        }
        return;

    // `read` answers with the most recent session; wait until it's ours.
    case Step::Read: {
        const qint64 session = reply.value("session_id").toInteger(-1);
        if (session == m_session) {
            const QString text = reply.value("text").toString().trimmed();
            if (!text.isEmpty()) {
                const QString path = m_current;
                finishCurrent();
                emit finished(path, text);
            } else if (reply.value("warning").toString().contains("speech", Qt::CaseInsensitive)) {
                fail(tr("No speech found in this memo."));
            } else {
                const QString why = reply.value("warning").toString(error);
                fail(tr("Superwhisper couldn't transcribe this memo: %1").arg(why.isEmpty() ? tr("no answer") : why));
            }
        } else if (session > m_session) {
            fail(tr("Superwhisper got busy with something else. Press T to try again."));
        } else if (++m_attempts < kMaxPolls) {
            m_poll.start();
        } else {
            fail(tr("Superwhisper took too long. Press T to try again."));
        }
        return;
    }
    }
}

void Transcriber::finishCurrent() {
    m_poll.stop();
    m_cloudAllowed.removeAll(m_current);
    m_current.clear();
    m_session = -1;
    m_step = Step::Idle;
    QTimer::singleShot(0, this, &Transcriber::next);
}

void Transcriber::fail(const QString &message) {
    const QString path = m_current;
    finishCurrent();
    emit failed(path, message);
}
