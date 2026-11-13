#include "backend.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QAudioSource>
#include <QClipboard>
#include <QGuiApplication>
#include <QSaveFile>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

namespace {

// Memos are named after the moment recording started, so the folder sorts by
// time in any file manager and the name alone says when it was said.
const QString kNameFormat = "yyyy-MM-dd HH-mm-ss";
const QString kPartialPrefix = ".recording.";

bool isAudio(const QFileInfo &file) {
    static const QStringList suffixes{"ogg", "opus", "oga", "m4a", "mp3", "wav", "flac", "aac", "webm"};
    return suffixes.contains(file.suffix().toLower());
}

} // namespace

Backend::Backend(const QString &directory, QObject *parent, const QString &superwhisper)
    : QObject(parent), m_directory(directory.isEmpty() ? defaultDirectory() : directory),
      m_transcriber(superwhisper) {
    if (!QDir().mkpath(m_directory))
        setError(tr("Couldn't create %1.").arg(m_directory));

    // A crash or power loss mid-recording leaves the partial file behind;
    // keep it as a memo rather than letting it vanish.
    recoverUnfinished(m_directory);

    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(120);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &Backend::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refreshDebounce.start(); });
    if (QFileInfo::exists(m_directory))
        m_watcher.addPath(m_directory);

    connect(&m_probe, &QFutureWatcher<QHash<QString, qint64>>::finished, this, [this] {
        const auto found = m_probe.result();
        for (auto it = found.begin(); it != found.end(); ++it)
            m_durations.insert(it.key(), it.value());
        for (auto &memo : m_memos)
            memo.duration = m_durations.value(memo.path, -1);
        emit memosChanged();
        if (m_probeAgain) {
            m_probeAgain = false;
            probeMissing();
        }
    });

    connect(&m_transcriber, &Transcriber::started, this, &Backend::memosChanged);
    connect(&m_transcriber, &Transcriber::finished, this, [this](const QString &path, const QString &text) {
        if (!writeTranscript(path, text))
            setError(tr("Couldn't save the transcript for %1.").arg(QFileInfo(path).fileName()));
        refresh();
    });
    connect(&m_transcriber, &Transcriber::failed, this, [this](const QString &, const QString &message) {
        setError(message);
        emit memosChanged();
    });
    connect(&m_transcriber, &Transcriber::cloudConsentNeeded, this, [this](const QString &path) {
        emit memosChanged();
        emit cloudConsentNeeded(path);
    });

    refresh();
}

Backend::~Backend() {
    m_probe.waitForFinished();
}

QString Backend::defaultDirectory() {
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty())
        music = QDir::homePath() + "/Music";
    return music + "/Memos";
}

bool Backend::recording() const {
    return m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState;
}

QVariantList Backend::memos() const {
    QVariantList list;
    list.reserve(m_memos.size());
    for (const auto &memo : m_memos) {
        list.append(QVariantMap{
            {"path", memo.path},
            {"url", QUrl::fromLocalFile(memo.path)},
            {"name", QFileInfo(memo.path).completeBaseName()},
            {"created", memo.created},
            {"duration", memo.duration},
            {"transcript", memo.transcript},
            {"transcribing", m_transcriber.isBusy(memo.path)},
        });
    }
    return list;
}

QDateTime Backend::createdAt(const QFileInfo &file) {
    // "2026-11-12 18-58-03 (2)" still parses: the counter only breaks ties.
    const QString base = file.completeBaseName().left(kNameFormat.size());
    const QDateTime parsed = QDateTime::fromString(base, kNameFormat);
    return parsed.isValid() ? parsed : file.lastModified();
}

QList<Memo> Backend::scan(const QString &directory) {
    QList<Memo> memos;
    const auto entries = QDir(directory).entryInfoList(QDir::Files | QDir::Readable);
    for (const auto &entry : entries) {
        if (entry.fileName().startsWith('.') || !isAudio(entry))
            continue;
        memos.append({entry.absoluteFilePath(), createdAt(entry), -1, readTranscript(entry.absoluteFilePath())});
    }
    std::sort(memos.begin(), memos.end(), [](const Memo &a, const Memo &b) {
        return a.created != b.created ? a.created > b.created : a.path > b.path;
    });
    return memos;
}

QString Backend::nextPath(const QString &directory, const QDateTime &when, const QString &suffix) {
    const QString base = directory + "/" + when.toString(kNameFormat);
    QString path = base + "." + suffix;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QString("%1 (%2).%3").arg(base).arg(n).arg(suffix);
    return path;
}

int Backend::recoverUnfinished(const QString &directory) {
    int recovered = 0;
    const auto entries = QDir(directory).entryInfoList({kPartialPrefix + "*"}, QDir::Files | QDir::Hidden);
    for (const auto &entry : entries) {
        if (entry.size() == 0) {
            QFile::remove(entry.absoluteFilePath());
            continue;
        }
        if (QFile::rename(entry.absoluteFilePath(), nextPath(directory, entry.lastModified(), entry.suffix())))
            ++recovered;
    }
    return recovered;
}

qint64 Backend::probeDuration(const QString &path) {
    QProcess ffprobe;
    ffprobe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                              "-of", "default=noprint_wrappers=1:nokey=1", path});
    if (!ffprobe.waitForFinished(5000) || ffprobe.exitCode() != 0)
        return -1;
    bool ok = false;
    const double seconds = QString::fromUtf8(ffprobe.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok ? qint64(seconds * 1000) : -1;
}

void Backend::refresh() {
    if (m_watcher.directories().isEmpty() && QFileInfo::exists(m_directory))
        m_watcher.addPath(m_directory);

    m_memos = scan(m_directory);
    for (auto &memo : m_memos)
        memo.duration = m_durations.value(memo.path, -1);
    emit memosChanged();
    probeMissing();
}

// Durations come from ffprobe off the UI thread; the list shows right away
// and fills them in when they arrive.
void Backend::probeMissing() {
    if (m_probe.isRunning()) {
        m_probeAgain = true;
        return;
    }
    QStringList missing;
    for (const auto &memo : m_memos)
        if (!m_durations.contains(memo.path))
            missing.append(memo.path);
    if (missing.isEmpty())
        return;
    m_probe.setFuture(QtConcurrent::run([missing] {
        QHash<QString, qint64> found;
        for (const auto &path : missing)
            found.insert(path, probeDuration(path));
        return found;
    }));
}

void Backend::toggleRecording() {
    if (m_saving)
        return;
    if (recording())
        stopRecording();
    else
        startRecording();
}

void Backend::stopThenQuit() {
    if (recording() || m_saving) {
        m_quitAfterSaving = true;
        if (recording())
            stopRecording();
    } else {
        QCoreApplication::quit();
    }
}

void Backend::startRecording() {
    clearError();
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        setError(tr("No microphone found."));
        return;
    }
    if (!QDir().mkpath(m_directory)) {
        setError(tr("Couldn't create %1.").arg(m_directory));
        return;
    }

    if (!m_recorder) {
        m_session = new QMediaCaptureSession(this);
        m_input = new QAudioInput(this);
        m_recorder = new QMediaRecorder(this);
        m_session->setAudioInput(m_input);
        m_session->setRecorder(m_recorder);

        connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::StoppedState && m_active)
                finishRecording();
            emit recordingChanged();
        });
        connect(m_recorder, &QMediaRecorder::durationChanged, this, [this](qint64 duration) {
            m_elapsed = duration;
            emit elapsedChanged();
        });
        connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &message) {
            setError(tr("Recording failed: %1").arg(message));
        });
    }

    // Follow whatever the desktop's default microphone is right now.
    m_input->setDevice(device);

    // Opus in Ogg is small and clear for speech; AAC is the fallback when the
    // multimedia backend can't encode Opus.
    QMediaFormat format(QMediaFormat::Ogg);
    format.setAudioCodec(QMediaFormat::AudioCodec::Opus);
    QString suffix = "ogg";
    if (!format.isSupported(QMediaFormat::Encode)) {
        format = QMediaFormat(QMediaFormat::Mpeg4Audio);
        format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        suffix = "m4a";
    }
    m_recorder->setMediaFormat(format);
    // Mono at 48 kbps is clear for a voice and keeps an hour under 25 MB.
    m_recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    m_recorder->setAudioChannelCount(1);
    m_recorder->setAudioBitRate(48000);

    // Record beside the final file under a hidden name, then rename once the
    // file is complete, so a half-written memo never shows up in the list.
    m_partial = m_directory + "/" + kPartialPrefix + suffix;
    QFile::remove(m_partial);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_partial));
    m_startedAt = QDateTime::currentDateTime();
    m_elapsed = 0;
    emit elapsedChanged();
    m_active = true;
    m_recorder->record();
    startLevels(device);
}

void Backend::stopRecording() {
    stopLevels();
    m_saving = true;
    emit recordingChanged();
    m_recorder->stop();
}

void Backend::finishRecording() {
    stopLevels();
    m_active = false;
    m_saving = false;
    QString written = m_recorder->actualLocation().toLocalFile();
    if (written.isEmpty())
        written = m_partial;

    if (QFileInfo(written).size() > 0) {
        const QString path = nextPath(m_directory, m_startedAt, QFileInfo(written).suffix());
        if (QFile::rename(written, path))
            m_durations.insert(path, m_elapsed);
        else
            setError(tr("Couldn't save the recording to %1.").arg(path));
    } else if (m_error.isEmpty()) {
        QFile::remove(written);
        setError(tr("Nothing was recorded. Check the microphone."));
    }

    m_elapsed = 0;
    emit elapsedChanged();
    refresh();
    if (m_quitAfterSaving)
        QCoreApplication::quit();
}

// The waveform listens to the same microphone on its own light stream: 16 kHz
// mono is plenty to see a voice, and it never touches what gets recorded.
void Backend::startLevels(const QAudioDevice &device) {
    stopLevels();
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format))
        format = device.preferredFormat();

    m_levelSource = new QAudioSource(device, format, this);
    m_levelSource->setBufferSize(format.bytesForDuration(100000));
    m_samplesPerBar = std::max(1, format.sampleRate() * 40 / 1000); // 25 bars a second
    m_peak = 0;
    m_samplesInBar = 0;
    m_levelDevice = m_levelSource->start();
    if (m_levelDevice)
        connect(m_levelDevice, &QIODevice::readyRead, this, &Backend::readLevels);
}

void Backend::stopLevels() {
    if (m_levelSource) {
        m_levelSource->stop();
        m_levelSource->deleteLater();
        m_levelSource = nullptr;
        m_levelDevice = nullptr;
    }
    m_levels.fill(0.0);
    emit levelsChanged();
}

void Backend::readLevels() {
    if (!m_levelDevice)
        return;
    const QByteArray data = m_levelDevice->readAll();
    const QAudioFormat format = m_levelSource->format();
    const int frameBytes = format.bytesPerFrame();
    if (frameBytes <= 0)
        return;

    bool changed = false;
    for (qsizetype offset = 0; offset + frameBytes <= data.size(); offset += frameBytes) {
        m_peak = std::max(m_peak, std::abs(format.normalizedSampleValue(data.constData() + offset)));
        if (++m_samplesInBar < m_samplesPerBar)
            continue;
        m_levels.removeFirst();
        m_levels.append(levelFor(m_peak));
        m_peak = 0;
        m_samplesInBar = 0;
        changed = true;
    }
    if (changed)
        emit levelsChanged();
}

// Peaks on a decibel scale, -54 dB to -6 dB, so a normal speaking voice fills
// the bars and room noise stays near the floor.
qreal Backend::levelFor(float peak) {
    const double db = 20.0 * std::log10(std::max(peak, 1e-6f));
    return std::clamp((db + 54.0) / 48.0, 0.0, 1.0);
}

// A transcript lives beside its memo with the same name, so the folder alone
// keeps both, and any editor or grep can read it.
QString Backend::transcriptPath(const QString &audioPath) {
    const QFileInfo info(audioPath);
    return info.absolutePath() + "/" + info.completeBaseName() + ".txt";
}

QString Backend::readTranscript(const QString &audioPath) {
    QFile file(transcriptPath(audioPath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool Backend::writeTranscript(const QString &audioPath, const QString &text) {
    QSaveFile file(transcriptPath(audioPath));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text.trimmed().toUtf8() + "\n");
    return file.commit();
}

void Backend::transcribe(const QString &path, bool allowCloud) {
    if (!QFileInfo::exists(path) || QFileInfo::exists(transcriptPath(path)))
        return;
    clearError();
    m_transcriber.transcribe(path, allowCloud);
}

void Backend::copyText(const QString &text) {
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

// Deleting moves the memo to the desktop trash, so even past undo it can be
// restored from the file manager.
bool Backend::trash(const QString &path) {
    QStringList files{path};
    if (QFileInfo::exists(transcriptPath(path)))
        files.append(transcriptPath(path));

    Trashed item;
    for (const auto &file : files) {
        QString inTrash;
        if (!QFile::moveToTrash(file, &inTrash)) {
            setError(tr("Couldn't move %1 to the trash.").arg(QFileInfo(file).fileName()));
            break;
        }
        item.originals.append(file);
        item.trashed.append(inTrash);
    }
    if (item.originals.isEmpty())
        return false;
    m_undo.append(item);
    return item.originals.size() == files.size();
}

bool Backend::remove(const QString &path) {
    if (!trash(path))
        return false;
    m_redo.clear();
    emit historyChanged();
    refresh();
    return true;
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const Trashed item = m_undo.takeLast();
    for (int i = 0; i < item.originals.size(); ++i) {
        const QString &original = item.originals[i];
        if (QFileInfo::exists(original) || !QFile::rename(item.trashed[i], original)) {
            setError(tr("Couldn't restore %1 from the trash.").arg(QFileInfo(original).fileName()));
            continue;
        }
        // The trash keeps a .trashinfo record beside each file; drop it so the
        // trash doesn't list a file that is no longer there.
        const QFileInfo trashed(item.trashed[i]);
        QFile::remove(trashed.dir().absolutePath() + "/../info/" + trashed.fileName() + ".trashinfo");
    }
    m_redo.append(item.originals.first());
    emit historyChanged();
    refresh();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    trash(m_redo.takeLast());
    emit historyChanged();
    refresh();
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::clearError() {
    setError({});
}
