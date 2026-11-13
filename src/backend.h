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

class QAudioInput;
class QMediaCaptureSession;

struct Memo {
    QString path;
    QDateTime created;
    qint64 duration = -1;
};

// Records voice memos into one folder and keeps the list of what's there.
// The folder is the source of truth: files dropped in or deleted elsewhere
// show up on their own.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY recordingChanged)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList memos READ memos NOTIFY memosChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    explicit Backend(const QString &directory = {}, QObject *parent = nullptr);
    ~Backend() override;

    bool recording() const;
    bool saving() const { return m_saving; }
    qint64 elapsed() const { return m_elapsed; }
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

    static QString defaultDirectory();
    static QList<Memo> scan(const QString &directory);
    static QString nextPath(const QString &directory, const QDateTime &when, const QString &suffix);
    static QDateTime createdAt(const QFileInfo &file);
    static qint64 probeDuration(const QString &path);
    static int recoverUnfinished(const QString &directory);

signals:
    void recordingChanged();
    void elapsedChanged();
    void errorChanged();
    void memosChanged();
    void historyChanged();

private:
    struct Trashed {
        QString original;
        QString trash;
    };

    void startRecording();
    void stopRecording();
    void finishRecording();
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

    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
    QFutureWatcher<QHash<QString, qint64>> m_probe;
    bool m_probeAgain = false;

    QList<Trashed> m_undo;
    QStringList m_redo;
};
