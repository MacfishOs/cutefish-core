#ifndef CUTEFISH_RECORDER_ENCODER_H
#define CUTEFISH_RECORDER_ENCODER_H

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

#include <deque>
#include <map>

#include "capturesession.h"

struct AVAudioFifo;
struct AVBufferRef;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVStream;
struct SwsContext;

// H.264 (VA-API, else x264) and AAC into an MP4, on its own thread. Frames keep
// the compositor's times; later sizes are letterboxed into the first one.
// DMA-BUF frames stay on the GPU: VA-API crops and converts them for the encoder.
class Encoder : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;

    explicit Encoder(QObject *parent = nullptr);
    ~Encoder() override;

public slots:
    //! Writes to @p path. The video's size is fixed by the first frame.
    void open(const QString &path, bool audio);

    //! @p crop of @p image, presented at @p presentedNs on CLOCK_MONOTONIC.
    //! @p buffer goes back through frameDone() once the pixels are read.
    void encodeVideo(const QImage &image, const QRect &crop, qint64 presentedNs, int buffer);
    //! The same from a DMA-BUF on the render node @p node. needsSharedMemory()
    //! when the GPU cannot take it.
    void encodeDmaBuf(const CaptureSession::DmaBuf &dmabuf, const QString &node,
                      const QRect &crop, qint64 presentedNs, int buffer);
    //! The capture buffers handed over so far are replaced, after a size change
    //! or by a new session; their GPU imports go with them.
    void forgetBuffers();

    //! Interleaved float samples whose first one was captured at @p capturedNs.
    void encodeAudio(const QByteArray &samples, qint64 capturedNs);

    //! Ends the video at @p stopNs and completes the file.
    void finish(qint64 stopNs);

signals:
    void frameDone(int buffer);
    //! Frames have to come in shared memory from now on.
    void needsSharedMemory();
    //! The first frame is in the file.
    void started();
    void finished(bool ok, const QString &error);

private:
    friend class ScreenRecorderEncoderTest;

    bool openVideo(const QSize &size, const QString &node, QString *error);
    bool startVideo(QString *error);
    void closeVideo();
    bool openVaapi(const QSize &size, const QString &node);
    bool openVpp();
    unsigned int importSurface(const CaptureSession::DmaBuf &dmabuf, int buffer);
    bool convertDmaBuf(const CaptureSession::DmaBuf &dmabuf, int buffer, const QRect &crop);
    //! @p source trimmed to the video, and where in the frame it goes (m_fit).
    QRect fit(QRect source);
    bool sendPending(qint64 presentedNs);
    void accepted(qint64 presentedNs);
    bool openX264(const QSize &size);
    bool openAudio(QString *error);
    bool writeHeader(QString *error);
    bool convert(const QImage &image, const QRect &crop);
    bool sendVideo(qint64 pts, qint64 duration);
    bool drainAudio(bool flush);
    bool writePackets(AVCodecContext *codec, AVStream *stream);
    void queueAudio(const QByteArray &samples, qint64 capturedNs);
    void fail(const QString &error);
    void close();

    QString m_path;
    bool m_wantsAudio = false;
    bool m_failed = false;

    AVFormatContext *m_format = nullptr;
    AVCodecContext *m_video = nullptr;
    AVCodecContext *m_audio = nullptr;
    AVStream *m_videoStream = nullptr;
    AVStream *m_audioStream = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    AVFrame *m_frame = nullptr;
    AVFrame *m_hwFrame = nullptr;
    AVFrame *m_audioFrame = nullptr;
    //! A converted GPU frame waiting for its duration.
    AVFrame *m_pendingHw = nullptr;
    void *m_display = nullptr;
    unsigned int m_vppConfig = 0;
    unsigned int m_vppContext = 0;
    //! Capture buffer id -> the VA surface it was imported as.
    std::map<int, unsigned int> m_imported;
    bool m_fitChanged = false;
    AVAudioFifo *m_fifo = nullptr;
    SwsContext *m_sws = nullptr;
    int m_swFormat = -1;
    QSize m_size;
    QRect m_fit;
    QSize m_scaledFrom;

    //! The first frame's presentation time; everything is relative to it.
    qint64 m_originNs = -1;
    //! The frame converted into m_frame, sent once the next one says how
    //! long it was on screen.
    qint64 m_pendingPts = -1;
    QHash<qint64, qint64> m_durations;
    qint64 m_audioSamples = 0;
    bool m_audioAligned = false;
    //! Audio that arrived before the first frame fixed the origin.
    std::deque<std::pair<QByteArray, qint64>> m_earlyAudio;
};

#endif // CUTEFISH_RECORDER_ENCODER_H
