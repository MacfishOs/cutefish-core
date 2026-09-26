#ifndef CUTEFISH_RECORDER_RECORDER_H
#define CUTEFISH_RECORDER_RECORDER_H

#include <QObject>
#include <QRect>
#include <QString>
#include <QThread>

#include "capturesession.h"

class AudioCapture;
class CaptureClient;
class Encoder;
class QSocketNotifier;
struct wl_display;

// One recording until stop(), written under a hidden name next to the file and
// renamed once it is complete.
class Recorder : public QObject
{
    Q_OBJECT

public:
    struct Options {
        //! The output to record, by wl_output name.
        QString output;
        //! Part of that output in global logical coordinates; empty for all of it.
        QRect region;
        //! Or a window, by its ext_foreign_toplevel_handle_v1 identifier.
        QString window;
        bool cursor = false;
        bool microphone = false;
        //! A PipeWire node name; empty for the default input.
        QString microphoneTarget;
        QString path;
    };

    explicit Recorder(const Options &options, QObject *parent = nullptr);
    ~Recorder() override;

    bool start(QString *error);
    void stop();

signals:
    void started();
    void finished(bool ok, const QString &error);

private:
    //! A session for the source, replacing the one there is.
    bool startSession(CaptureSession::Memory memory);
    void dispatch();
    void captureNext();
    //! The part of a frame of this size that is recorded.
    QRect cropFor(const QSize &frame) const;
    void onFrame(const CaptureSession::Frame &frame);
    void onEncoderFinished(bool ok, const QString &error);
    QString partialPath() const;

    Options m_options;
    wl_display *m_display = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    CaptureClient *m_client = nullptr;
    CaptureSession *m_session = nullptr;
    CaptureSession::Memory m_memory = CaptureSession::Memory::DmaBuf;
    int m_generation = 0;
    AudioCapture *m_audio = nullptr;
    QThread m_encoderThread;
    Encoder *m_encoder = nullptr;
    QRect m_outputGeometry;
    bool m_stopping = false;
    bool m_done = false;
};

#endif // CUTEFISH_RECORDER_RECORDER_H
