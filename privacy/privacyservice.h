// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "captureswitches.h"
#include "usbcameras.h"

#include <QDBusContext>
#include <QObject>
#include <QTimer>

#include <functional>

// The system's camera and microphone switches, for every user and process.
// A device turned off leaves a file in the state directory, which the udev
// rules test to keep it off at boot and when plugged in.
class PrivacyService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(bool cameraEnabled READ cameraEnabled NOTIFY cameraEnabledChanged)
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled NOTIFY microphoneEnabledChanged)

public:
    explicit PrivacyService(const QString &stateDirectory, QObject *parent = nullptr);

    bool cameraEnabled() const;
    bool microphoneEnabled() const;

public Q_SLOTS:
    void setCameraEnabled(bool enabled);
    void setMicrophoneEnabled(bool enabled);

Q_SIGNALS:
    void cameraEnabledChanged(bool enabled);
    void microphoneEnabledChanged(bool enabled);

    // Nothing called for a while and no lock needs this process alive.
    void idle();

private:
    void authorize(std::function<void()> apply);
    void applyCamera(bool enabled);
    void applyMicrophone(bool enabled);
    QString flagPath(const char *device) const;
    void setFlag(const char *device, bool off);

    QString m_stateDirectory;
    UsbCameras m_cameras;
    CaptureSwitches m_captureSwitches;
    QTimer m_idleTimer;
};
