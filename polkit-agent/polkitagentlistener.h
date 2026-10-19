// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef POLKITAGENTLISTENER_H
#define POLKITAGENTLISTENER_H

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "base/dbus.h"

class Dialog;
class AuthSession;

// org.freedesktop.PolicyKit1.AuthenticationAgent for the session: one dialog
// at a time, answered through polkit's helper.
class PolKitAgentListener
{
public:
    // a(sa{sv}): each identity's kind and details.
    using Identity = std::tuple<std::string, cutefish::base::dbus::VariantMap>;

    explicit PolKitAgentListener(cutefish::base::dbus::Connection *bus);
    ~PolKitAgentListener();

    PolKitAgentListener(const PolKitAgentListener &) = delete;
    PolKitAgentListener &operator=(const PolKitAgentListener &) = delete;

    bool registerAgent(const std::string &objectPath);

    // The open dialog, for tests.
    Dialog *dialog() const { return m_dialog.get(); }

private:
    enum Outcome {
        Completed,
        Dismissed,
        Withdrawn,
    };

    struct Request {
        std::string message;
        std::string cookie;
        std::string user;
        cutefish::base::dbus::MethodCall call;
    };

    void beginAuthentication(cutefish::base::dbus::MethodCall &call);
    void cancelAuthentication(const std::string &cookie);

    bool sendRegistration();
    void onPolkitOwnerChanged(const std::string &oldOwner, const std::string &newOwner);
    void dropRequests();

    void startNext();
    void startSession();
    void releaseSession();
    void releaseDialog();
    void finishCurrent(Outcome outcome);
    void reply(Request &request, Outcome outcome);

    void onRequest(const std::string &prompt, bool echo);
    void onNotice(const std::string &text, bool error);
    void onCompleted(bool gainedAuthorization);
    void onDialogAccepted();

    cutefish::base::dbus::Connection *m_bus;
    std::string m_objectPath;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    cutefish::base::dbus::Match m_ownerChanged;

    // polkitd sends requests concurrently; the first one owns the dialog.
    std::deque<Request> m_queue;

    std::unique_ptr<Dialog> m_dialog;
    std::unique_ptr<AuthSession> m_session;

    // A response typed before PAM asked for one.
    std::string m_response;
    bool m_hasResponse = false;
    bool m_requestPending = false;

    int m_numTries = 0;
};

#endif // POLKITAGENTLISTENER_H
