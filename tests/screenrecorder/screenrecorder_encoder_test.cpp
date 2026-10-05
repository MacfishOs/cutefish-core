#include "encoder.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <vector>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#include <va/va.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

static constexpr qint64 kOriginNs = 1'000'000'000'000;
static constexpr qint64 kFrameNs = 33'333'333;
static constexpr char kRenderNode[] = "/dev/dri/renderD128";

static QImage frame(const QSize &size, int index)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor::fromHsv((index * 12) % 360, 200, 220));
    return image;
}

static QByteArray tone(int frames, int offset)
{
    QByteArray samples(frames * Encoder::kChannels * int(sizeof(float)), Qt::Uninitialized);
    auto *data = reinterpret_cast<float *>(samples.data());
    for (int i = 0; i < frames; ++i) {
        const float value = 0.3f * std::sin(2.0f * float(M_PI) * 440.0f * float(offset + i)
                                            / Encoder::kSampleRate);
        data[i * 2] = value;
        data[i * 2 + 1] = value;
    }
    return samples;
}

namespace {

struct Probe {
    int videoStreams = 0;
    int audioStreams = 0;
    QSize size;
    int sampleRate = 0;
    double duration = 0;
    AVCodecID videoCodec = AV_CODEC_ID_NONE;
    AVCodecID audioCodec = AV_CODEC_ID_NONE;
};

} // namespace

static Probe probe(const QString &path)
{
    Probe result;
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0)
        return result;
    avformat_find_stream_info(format, nullptr);
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVCodecParameters *codec = format->streams[i]->codecpar;
        if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            ++result.videoStreams;
            result.size = QSize(codec->width, codec->height);
            result.videoCodec = codec->codec_id;
        } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++result.audioStreams;
            result.sampleRate = codec->sample_rate;
            result.audioCodec = codec->codec_id;
        }
    }
    result.duration = double(format->duration) / AV_TIME_BASE;
    avformat_close_input(&format);
    return result;
}

// The luma plane of the last frame in the file.
static QImage lastLuma(const QString &path)
{
    QImage result;
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0)
        return result;
    avformat_find_stream_info(format, nullptr);
    const int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec *decoder = avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
    AVCodecContext *context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(context, format->streams[stream]->codecpar);
    avcodec_open2(context, decoder, nullptr);
    AVPacket *packet = av_packet_alloc();
    AVFrame *decoded = av_frame_alloc();
    const auto take = [&] {
        while (avcodec_receive_frame(context, decoded) >= 0) {
            result = QImage(decoded->width, decoded->height, QImage::Format_Grayscale8);
            for (int y = 0; y < decoded->height; ++y)
                memcpy(result.scanLine(y), decoded->data[0] + y * decoded->linesize[0],
                       size_t(decoded->width));
        }
    };
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream && avcodec_send_packet(context, packet) >= 0)
            take();
        av_packet_unref(packet);
    }
    avcodec_send_packet(context, nullptr);
    take();
    av_frame_free(&decoded);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    avformat_close_input(&format);
    return result;
}

namespace {

// DMA-BUFs on a render node, allocated the way the capture session does.
class RenderNode
{
public:
    RenderNode()
        : m_fd(open(kRenderNode, O_RDWR | O_CLOEXEC))
    {
        if (m_fd >= 0)
            m_gbm = gbm_create_device(m_fd);
    }

    ~RenderNode()
    {
        freeBuffers();
        if (m_gbm)
            gbm_device_destroy(m_gbm);
        if (m_fd >= 0)
            close(m_fd);
    }

    bool isValid() const { return m_gbm != nullptr; }

    CaptureSession::DmaBuf allocate(const QSize &size)
    {
        const auto width = uint32_t(size.width());
        const auto height = uint32_t(size.height());
        const uint64_t linear = DRM_FORMAT_MOD_LINEAR;
        gbm_bo *bo = gbm_bo_create_with_modifiers2(m_gbm, width, height, DRM_FORMAT_XRGB8888,
                                                   &linear, 1, GBM_BO_USE_RENDERING);
        if (!bo)
            bo = gbm_bo_create(m_gbm, width, height, DRM_FORMAT_XRGB8888,
                               GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
        CaptureSession::DmaBuf dmabuf;
        if (!bo)
            return dmabuf;
        dmabuf.fd = gbm_bo_get_fd(bo);
        dmabuf.format = DRM_FORMAT_XRGB8888;
        dmabuf.modifier = gbm_bo_get_modifier(bo);
        dmabuf.offset = gbm_bo_get_offset(bo, 0);
        dmabuf.stride = gbm_bo_get_stride_for_plane(bo, 0);
        dmabuf.size = size;
        m_buffers.emplace_back(bo, dmabuf.fd);
        return dmabuf;
    }

    // What the session does with its buffers when the size changes.
    void freeBuffers()
    {
        for (const auto &[bo, fd] : m_buffers) {
            if (fd >= 0)
                close(fd);
            gbm_bo_destroy(bo);
        }
        m_buffers.clear();
    }

private:
    int m_fd = -1;
    gbm_device *m_gbm = nullptr;
    std::vector<std::pair<gbm_bo *, int>> m_buffers;
};

} // namespace

class ScreenRecorderEncoderTest : public QObject
{
    Q_OBJECT

private slots:
    void writesVideoAndSoundUntilTheStop();
    void laterSizesAreScaledIntoTheFirst();
    void oddSizesAreTrimmedToEven();
    void aRecordingWithoutFramesFails();
    void replacedBuffersLoseTheirImports();
};

void ScreenRecorderEncoderTest::writesVideoAndSoundUntilTheStop()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("recording.mp4"));
    Encoder encoder;
    QSignalSpy started(&encoder, &Encoder::started);
    QSignalSpy done(&encoder, &Encoder::frameDone);
    QSignalSpy finished(&encoder, &Encoder::finished);

    encoder.open(path, true);
    // Sound from before the first frame is cut at it.
    for (int chunk = -5; chunk < 100; ++chunk)
        encoder.encodeAudio(tone(480, chunk * 480), kOriginNs + chunk * 10'000'000);
    for (int i = 0; i < 30; ++i) {
        const QImage image = frame(QSize(320, 240), i);
        encoder.encodeVideo(image, image.rect(), kOriginNs + i * kFrameNs, i);
    }
    // The screen stood still for half a second before the stop.
    encoder.finish(kOriginNs + 1'500'000'000);

    QCOMPARE(started.count(), 1);
    QCOMPARE(done.count(), 30);
    QCOMPARE(done.last().at(0).toInt(), 29);
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));

    const Probe file = probe(path);
    QCOMPARE(file.videoStreams, 1);
    QCOMPARE(file.audioStreams, 1);
    QCOMPARE(file.videoCodec, AV_CODEC_ID_H264);
    QCOMPARE(file.audioCodec, AV_CODEC_ID_AAC);
    QCOMPARE(file.size, QSize(320, 240));
    QCOMPARE(file.sampleRate, Encoder::kSampleRate);
    QVERIFY2(std::abs(file.duration - 1.5) < 0.1, qPrintable(QString::number(file.duration)));
}

void ScreenRecorderEncoderTest::laterSizesAreScaledIntoTheFirst()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("window.mp4"));
    Encoder encoder;
    QSignalSpy finished(&encoder, &Encoder::finished);

    encoder.open(path, false);
    const QImage first = frame(QSize(320, 240), 0);
    encoder.encodeVideo(first, first.rect(), kOriginNs, -1);
    // The window became twice as wide: it is shown whole, with bars above and below.
    QImage wide(QSize(640, 240), QImage::Format_ARGB32_Premultiplied);
    wide.fill(Qt::white);
    encoder.encodeVideo(wide, wide.rect(), kOriginNs + kFrameNs, -1);
    encoder.finish(kOriginNs + 2 * kFrameNs);

    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(probe(path).size, QSize(320, 240));

    const QImage luma = lastLuma(path);
    QCOMPARE(luma.size(), QSize(320, 240));
    QVERIFY(qGray(luma.pixel(160, 10)) < 40);
    QVERIFY(qGray(luma.pixel(160, 230)) < 40);
    QVERIFY(qGray(luma.pixel(160, 120)) > 200);
}

void ScreenRecorderEncoderTest::oddSizesAreTrimmedToEven()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("portion.mp4"));
    Encoder encoder;
    QSignalSpy finished(&encoder, &Encoder::finished);

    encoder.open(path, false);
    const QImage screen = frame(QSize(400, 300), 3);
    encoder.encodeVideo(screen, QRect(11, 21, 201, 151), kOriginNs, -1);
    encoder.finish(kOriginNs + kFrameNs);

    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(probe(path).size, QSize(200, 150));
}

void ScreenRecorderEncoderTest::aRecordingWithoutFramesFails()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("empty.mp4"));
    Encoder encoder;
    QSignalSpy finished(&encoder, &Encoder::finished);

    encoder.open(path, true);
    encoder.encodeAudio(tone(480, 0), kOriginNs);
    encoder.finish(kOriginNs + kFrameNs);

    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(!QFile::exists(path));
}

void ScreenRecorderEncoderTest::replacedBuffersLoseTheirImports()
{
    RenderNode gpu;
    if (!gpu.isValid())
        QSKIP("no render node to allocate DMA-BUFs on");

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("resized.mp4"));
    const QString node = QString::fromLatin1(kRenderNode);
    Encoder encoder;
    QSignalSpy shm(&encoder, &Encoder::needsSharedMemory);
    QSignalSpy done(&encoder, &Encoder::frameDone);
    QSignalSpy finished(&encoder, &Encoder::finished);
    encoder.open(path, false);

    // Three buffers taking turns, the way a session hands them out.
    qint64 presentedNs = kOriginNs;
    const auto record = [&](const QSize &size, int firstBuffer) {
        CaptureSession::DmaBuf buffers[3];
        for (CaptureSession::DmaBuf &buffer : buffers)
            buffer = gpu.allocate(size);
        for (int i = 0; i < 6; ++i) {
            encoder.encodeDmaBuf(buffers[i % 3], node, QRect(QPoint(), size), presentedNs,
                                 firstBuffer + i % 3);
            presentedNs += kFrameNs;
        }
        return buffers[0].isValid() && buffers[1].isValid() && buffers[2].isValid();
    };

    QVERIFY(record(QSize(320, 240), 0));
    if (!shm.isEmpty())
        QSKIP("this device cannot convert DMA-BUF frames with VA-API");
    QCOMPARE(encoder.m_imported.size(), size_t(3));
    std::vector<unsigned int> surfaces;
    for (const auto &[buffer, surface] : encoder.m_imported)
        surfaces.push_back(surface);

    // The window was resized: the session frees its buffers for three new ones.
    gpu.freeBuffers();
    encoder.forgetBuffers();
    QVERIFY(encoder.m_imported.empty());
    for (unsigned int surface : surfaces) {
        VASurfaceStatus status = VASurfaceReady;
        QVERIFY(vaQuerySurfaceStatus(encoder.m_display, surface, &status) != VA_STATUS_SUCCESS);
    }

    QVERIFY(record(QSize(640, 240), 3));
    QVERIFY(shm.isEmpty());
    QCOMPARE(encoder.m_imported.size(), size_t(3));
    encoder.finish(presentedNs);

    QCOMPARE(done.count(), 12);
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(probe(path).size, QSize(320, 240));
}

QTEST_GUILESS_MAIN(ScreenRecorderEncoderTest)

#include "screenrecorder_encoder_test.moc"
