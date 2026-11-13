#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QMediaRecorder>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "transcriber.h"

class QAudioInput;
class QAudioSource;
class QIODevice;
class QMediaCaptureSession;

struct Memo {
    QString path;
    QDateTime created;
    qint64 duration = -1;
    QString transcript;
};

// Records voice memos into one folder and keeps the list of what's there.
// The folder is the source of truth: files dropped in or deleted elsewhere
// show up on their own.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY recordingChanged)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(QList<qreal> levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList memos READ memos NOTIFY memosChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    explicit Backend(const QString &directory = {}, QObject *parent = nullptr,
                     const QString &superwhisper = "superwhisper");
    ~Backend() override;

    bool recording() const;
    bool saving() const { return m_saving; }
    qint64 elapsed() const { return m_elapsed; }
    QList<qreal> levels() const { return m_levels; }
    QString error() const { return m_error; }
    QVariantList memos() const;
    QString directory() const { return m_directory; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void stopThenQuit();
    Q_INVOKABLE bool remove(const QString &path);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void transcribe(const QString &path, bool allowCloud = false);
    Q_INVOKABLE void copyText(const QString &text);

    Transcriber *transcriber() { return &m_transcriber; }

    static QString defaultDirectory();
    static QList<Memo> scan(const QString &directory);
    static QString nextPath(const QString &directory, const QDateTime &when, const QString &suffix);
    static QDateTime createdAt(const QFileInfo &file);
    static qint64 probeDuration(const QString &path);
    static int recoverUnfinished(const QString &directory);
    static qreal levelFor(float peak);
    static QString transcriptPath(const QString &audioPath);
    static QString readTranscript(const QString &audioPath);
    static bool writeTranscript(const QString &audioPath, const QString &text);

    static constexpr int kBars = 48;

signals:
    void recordingChanged();
    void elapsedChanged();
    void levelsChanged();
    void errorChanged();
    void memosChanged();
    void historyChanged();
    void cloudConsentNeeded(const QString &path);

private:
    // A memo and its transcript go to the trash, and come back, together.
    struct Trashed {
        QStringList originals;
        QStringList trashed;
    };

    void startRecording();
    void stopRecording();
    void finishRecording();
    void startLevels(const class QAudioDevice &device);
    void stopLevels();
    void readLevels();
    bool trash(const QString &path);
    void setError(const QString &error);
    void probeMissing();

    QString m_directory;
    QList<Memo> m_memos;
    QHash<QString, qint64> m_durations;

    QMediaCaptureSession *m_session = nullptr;
    QAudioInput *m_input = nullptr;
    QMediaRecorder *m_recorder = nullptr;
    QString m_partial;
    QDateTime m_startedAt;
    qint64 m_elapsed = 0;
    bool m_active = false;
    bool m_saving = false;
    bool m_quitAfterSaving = false;
    QString m_error;

    QAudioSource *m_levelSource = nullptr;
    QIODevice *m_levelDevice = nullptr;
    QList<qreal> m_levels = QList<qreal>(kBars, 0.0);
    float m_peak = 0;
    int m_samplesInBar = 0;
    int m_samplesPerBar = 640;

    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
    QFutureWatcher<QHash<QString, qint64>> m_probe;
    bool m_probeAgain = false;

    Transcriber m_transcriber;

    QList<Trashed> m_undo;
    QStringList m_redo;
};
