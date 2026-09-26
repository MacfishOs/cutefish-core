#ifndef CUTEFISH_RECORDER_AUDIOCAPTURE_H
#define CUTEFISH_RECORDER_AUDIOCAPTURE_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <pipewire/pipewire.h>

// The microphone as interleaved float stereo at the encoder's rate; samples()
// is emitted on PipeWire's thread.
class AudioCapture : public QObject
{
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    //! @p target is a node name; empty records the default input.
    bool start(const QString &target, QString *error);
    void stop();

signals:
    //! @p capturedNs is when the first sample was taken, on CLOCK_MONOTONIC.
    void samples(const QByteArray &interleaved, qint64 capturedNs);

private:
    static const pw_stream_events s_events;
    void process();

    pw_thread_loop *m_loop = nullptr;
    pw_stream *m_stream = nullptr;
};

#endif // CUTEFISH_RECORDER_AUDIOCAPTURE_H
