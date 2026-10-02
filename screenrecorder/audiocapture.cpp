#include "audiocapture.h"
#include "encoder.h"
#include "monotonic.h"

#include <spa/param/audio/format-utils.h>

const pw_stream_events AudioCapture::s_events = [] {
    pw_stream_events events {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = [](void *data) {
        static_cast<AudioCapture *>(data)->process();
    };
    return events;
}();

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
}

AudioCapture::~AudioCapture()
{
    stop();
    pw_deinit();
}

bool AudioCapture::start(const QString &target, QString *error)
{
    m_loop = pw_thread_loop_new("cutefish-recorder-audio", nullptr);
    if (!m_loop) {
        *error = QStringLiteral("cannot start a PipeWire loop");
        return false;
    }

    pw_properties *properties = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
            "Production", PW_KEY_APP_NAME, "Screen Recording", PW_KEY_NODE_NAME,
            "cutefish-screenrecorder", nullptr);
    if (!target.isEmpty())
        pw_properties_set(properties, PW_KEY_TARGET_OBJECT, target.toUtf8().constData());

    pw_thread_loop_lock(m_loop);
    m_stream = pw_stream_new_simple(pw_thread_loop_get_loop(m_loop), "Screen Recording",
                                    properties, &s_events, this);
    if (!m_stream) {
        pw_thread_loop_unlock(m_loop);
        *error = QStringLiteral("cannot create a PipeWire stream");
        return false;
    }

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = Encoder::kSampleRate;
    info.channels = Encoder::kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    const int result = pw_stream_connect(
            m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
            params, 1);
    if (result < 0 || pw_thread_loop_start(m_loop) < 0) {
        pw_thread_loop_unlock(m_loop);
        *error = QStringLiteral("cannot connect to the microphone");
        return false;
    }
    pw_thread_loop_unlock(m_loop);
    return true;
}

void AudioCapture::stop()
{
    if (!m_loop)
        return;
    pw_thread_loop_lock(m_loop);
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_stop(m_loop);
    pw_thread_loop_destroy(m_loop);
    m_loop = nullptr;
}

void AudioCapture::process()
{
    pw_buffer *buffer = pw_stream_dequeue_buffer(m_stream);
    if (!buffer)
        return;
    const spa_data &data = buffer->buffer->datas[0];
    if (data.data && data.chunk && data.chunk->size > 0) {
        const auto *bytes = static_cast<const char *>(data.data) + data.chunk->offset;
        const qsizetype size = data.chunk->size;
        const qint64 frames = size / qsizetype(sizeof(float) * Encoder::kChannels);
        // The buffer is complete now; its first sample is that much older.
        const qint64 capturedNs = monotonicNs() - frames * 1000000000 / Encoder::kSampleRate;
        emit samples(QByteArray(bytes, size), capturedNs);
    }
    pw_stream_queue_buffer(m_stream, buffer);
}
