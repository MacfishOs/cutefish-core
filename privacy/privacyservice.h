// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "captureswitches.h"
#include "usbcameras.h"

#include "base/dbus.h"
#include "base/timer.h"

#include <functional>
#include <memory>
#include <string>

// The system's camera and microphone switches, for every user and process.
// A device turned off leaves a file in the state directory, which the udev
// rules test to keep it off at boot and when plugged in.
class PrivacyService
{
public:
    explicit PrivacyService(const std::string &stateDirectory);

    bool cameraEnabled() const;
    bool microphoneEnabled() const;

    // Without a D-Bus call to answer, as for a call from this process.
    void setCameraEnabled(bool enabled);
    void setMicrophoneEnabled(bool enabled);

    // Exports com.cutefish.Privacy at /com/cutefish/Privacy on the bus; false
    // when the object cannot be added.
    bool exportTo(cutefish::base::dbus::Connection *bus);

    // Nothing called for a while and no lock needs this process alive.
    std::function<void()> onIdle;

private:
    // Applies after polkit allowed the caller, and answers the call.
    void authorize(cutefish::base::dbus::MethodCall &call, std::function<void()> apply);
    void applyCamera(bool enabled);
    void applyMicrophone(bool enabled);
    std::string flagPath(const char *device) const;
    void setFlag(const char *device, bool off);

    std::string m_stateDirectory;
    UsbCameras m_cameras;
    CaptureSwitches m_captureSwitches;
    cutefish::base::Timer m_idleTimer;
    cutefish::base::dbus::Connection *m_bus = nullptr;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
};
