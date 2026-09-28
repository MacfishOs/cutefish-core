#include "recorder.h"
#include "audiocapture.h"
#include "captureclient.h"
#include "encoder.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <ctime>

#include <wayland-client.h>

namespace {

// Buffer ids travel to the encoder with the session they belong to.
constexpr int kSessionStride = 1 << 16;

qint64 monotonicNs()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return qint64(now.tv_sec) * 1000000000 + now.tv_nsec;
}

} // namespace

Recorder::Recorder(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
    qRegisterMetaType<CaptureSession::Frame>();
    qRegisterMetaType<CaptureSession::DmaBuf>();
}

Recorder::~Recorder()
{
    if (m_audio)
        m_audio->stop();
    m_encoderThread.quit();
    m_encoderThread.wait();
    delete m_session;
    delete m_client;
    if (m_display)
        wl_display_disconnect(m_display);
}

bool Recorder::start(QString *error)
{
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        *error = QStringLiteral("cannot connect to the compositor");
        return false;
    }
    m_client = new CaptureClient(m_display, this);
    if (!m_client->initialize()) {
        *error = QStringLiteral("the compositor does not let this program capture the screen");
        return false;
    }

    if (m_options.window.isEmpty()) {
        if (const CaptureClient::Output *output = m_client->outputNamed(m_options.output))
            m_outputGeometry = output->logicalGeometry;
    }

    m_notifier = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Recorder::dispatch);
    // Events a round trip already read sit in the queue without the socket
    // becoming readable again.
    connect(QCoreApplication::eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, this,
            [this] {
                wl_display_dispatch_pending(m_display);
                wl_display_flush(m_display);
            });

    m_encoder = new Encoder;
    m_encoder->moveToThread(&m_encoderThread);
    connect(&m_encoderThread, &QThread::finished, m_encoder, &QObject::deleteLater);
    connect(m_encoder, &Encoder::frameDone, this, [this](int token) {
        // A buffer of a session replaced since is already gone.
        if (token >= 0 && m_session && token / kSessionStride == m_generation)
            m_session->releaseBuffer(token % kSessionStride);
        captureNext();
    });
    connect(m_encoder, &Encoder::needsSharedMemory, this, [this] {
        if (m_memory == CaptureSession::Memory::DmaBuf)
            startSession(CaptureSession::Memory::Shm);
    });
    connect(m_encoder, &Encoder::started, this, &Recorder::started);
    connect(m_encoder, &Encoder::finished, this, &Recorder::onEncoderFinished);
    m_encoderThread.start();
    QMetaObject::invokeMethod(m_encoder, "open", Q_ARG(QString, partialPath()),
                              Q_ARG(bool, m_options.microphone));

    // Frames on the GPU first; the encoder asks for shared memory without it.
    if (!startSession(CaptureSession::Memory::DmaBuf)) {
        *error = QStringLiteral("there is nothing to record");
        return false;
    }

    if (m_options.microphone) {
        m_audio = new AudioCapture(this);
        connect(m_audio, &AudioCapture::samples, m_encoder, &Encoder::encodeAudio);
        QString audioError;
        if (!m_audio->start(m_options.microphoneTarget, &audioError))
            qWarning("cutefish-screenrecorder: %s", qPrintable(audioError));
    }
    return true;
}

bool Recorder::startSession(CaptureSession::Memory memory)
{
    ext_image_capture_source_v1 *source = m_options.window.isEmpty()
            ? m_client->createOutputSource(m_options.output)
            : m_client->createWindowSource(m_options.window);
    if (!source)
        return false;

    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
    }
    ++m_generation;
    m_memory = memory;
    // Three buffers: one being captured, one being encoded and one to spare.
    m_session = new CaptureSession(m_client, source, m_options.cursor, 3, memory, this);
    connect(m_session, &CaptureSession::constraintsChanged, this, [this] {
        // New buffers replace the old ones, which the encoder may still have
        // imported; queued behind the frames it has yet to encode from them.
        QMetaObject::invokeMethod(m_encoder, "forgetBuffers");
        captureNext();
    });
    connect(m_session, &CaptureSession::frameReady, this, &Recorder::onFrame);
    connect(m_session, &CaptureSession::frameFailed, this, &Recorder::captureNext,
            Qt::QueuedConnection);
    // A window that closes ends its recording with what there is.
    connect(m_session, &CaptureSession::stopped, this, &Recorder::stop);
    return true;
}

void Recorder::stop()
{
    if (m_stopping || m_done)
        return;
    m_stopping = true;
    const qint64 stopNs = monotonicNs();
    if (m_audio)
        m_audio->stop();
    QMetaObject::invokeMethod(m_encoder, "finish", Q_ARG(qint64, stopNs));
}

void Recorder::dispatch()
{
    if (wl_display_dispatch(m_display) < 0) {
        m_notifier->setEnabled(false);
        stop();
        return;
    }
    wl_display_flush(m_display);
}

void Recorder::captureNext()
{
    if (!m_stopping && m_session)
        m_session->capture();
}

void Recorder::onFrame(const CaptureSession::Frame &frame)
{
    if (m_stopping) {
        m_session->releaseBuffer(frame.buffer);
        return;
    }

    if (frame.dmabuf.isValid()) {
        // The GPU path takes the pixels as they are; a rotated output goes
        // through shared memory, where the copy can be turned upright.
        if (frame.transform != 0) {
            m_session->releaseBuffer(frame.buffer);
            startSession(CaptureSession::Memory::Shm);
            return;
        }
        QMetaObject::invokeMethod(m_encoder, "encodeDmaBuf",
                                  Q_ARG(CaptureSession::DmaBuf, frame.dmabuf),
                                  Q_ARG(QString, m_session->renderNode()),
                                  Q_ARG(QRect, cropFor(frame.dmabuf.size)),
                                  Q_ARG(qint64, frame.presentedNs),
                                  Q_ARG(int, m_generation * kSessionStride + frame.buffer));
        captureNext();
        return;
    }

    // A rotated output gets an upright copy, but its buffer stays held until the
    // encoder is done, so the pool still bounds the frames in flight.
    const QImage image = CaptureSession::upright(frame.image, frame.transform);
    QMetaObject::invokeMethod(m_encoder, "encodeVideo", Q_ARG(QImage, image),
                              Q_ARG(QRect, cropFor(image.size())),
                              Q_ARG(qint64, frame.presentedNs),
                              Q_ARG(int, m_generation * kSessionStride + frame.buffer));
    // The other buffers are free while this one is encoded.
    captureNext();
}

QRect Recorder::cropFor(const QSize &frame) const
{
    const QRect whole(QPoint(), frame);
    if (!m_options.window.isEmpty() || m_options.region.isEmpty() || m_outputGeometry.isEmpty())
        return whole;
    const double scale = double(frame.width()) / m_outputGeometry.width();
    const QRect logical = m_options.region.translated(-m_outputGeometry.topLeft());
    return QRectF(logical.x() * scale, logical.y() * scale, logical.width() * scale,
                  logical.height() * scale)
                   .toAlignedRect()
            & whole;
}

void Recorder::onEncoderFinished(bool ok, const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    m_stopping = true;
    if (m_audio)
        m_audio->stop();

    QString message = error;
    if (ok) {
        QFile::remove(m_options.path);
        if (!QFile::rename(partialPath(), m_options.path)) {
            ok = false;
            message = QStringLiteral("cannot name the recording %1").arg(m_options.path);
        }
    }
    if (!ok)
        QFile::remove(partialPath());
    emit finished(ok, message);
}

QString Recorder::partialPath() const
{
    const QFileInfo info(m_options.path);
    return info.dir().filePath(QLatin1Char('.') + info.fileName() + QStringLiteral(".part"));
}
