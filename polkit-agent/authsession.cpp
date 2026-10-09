// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "authsession.h"

#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QDebug>

static const QString helperPath = QStringLiteral("/usr/lib/polkit-1/polkit-agent-helper-1");
static const QString helperSocketPath = QStringLiteral("/run/polkit/agent-helper.socket");

// The helper escapes each message the way g_strescape() does: C escapes and
// octal for control and non-ASCII bytes.
static QString unescape(const QByteArray &text)
{
    QByteArray bytes;
    bytes.reserve(text.size());

    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != '\\' || i + 1 == text.size()) {
            bytes.append(text.at(i));
            continue;
        }

        const char c = text.at(++i);

        if (c >= '0' && c <= '7') {
            int value = 0;
            int digits = 0;

            while (digits < 3 && i < text.size() && text.at(i) >= '0' && text.at(i) <= '7') {
                value = value * 8 + (text.at(i) - '0');
                ++i;
                ++digits;
            }

            --i;
            bytes.append(char(value));
            continue;
        }

        switch (c) {
        case 'b': bytes.append('\b'); break;
        case 'f': bytes.append('\f'); break;
        case 'n': bytes.append('\n'); break;
        case 'r': bytes.append('\r'); break;
        case 't': bytes.append('\t'); break;
        case 'v': bytes.append('\v'); break;
        default: bytes.append(c); break;
        }
    }

    return QString::fromUtf8(bytes);
}

AuthSession::AuthSession(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_process(nullptr)
    , m_channel(nullptr)
    , m_succeeded(false)
    , m_finished(false)
{
}

AuthSession::~AuthSession()
{
    m_finished = true;

    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }

    if (m_process) {
        m_process->disconnect(this);

        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

void AuthSession::start(const QString &user, const QString &cookie)
{
    if (!QFile::exists(helperSocketPath) || !connectSocket(user))
        startProcess(user);

    // The cookie goes through the channel so other processes cannot read it.
    m_channel->write(cookie.toUtf8() + '\n');
}

void AuthSession::respond(const QString &response)
{
    QByteArray line = response.toUtf8();
    line.replace('\n', ' ');

    m_channel->write(line + '\n');
}

bool AuthSession::connectSocket(const QString &user)
{
    m_socket = new QLocalSocket(this);
    m_socket->connectToServer(helperSocketPath);

    if (!m_socket->waitForConnected(1000)) {
        qWarning() << "Cannot connect to" << helperSocketPath << m_socket->errorString()
                   << "- falling back to" << helperPath;
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    m_channel = m_socket;

    connect(m_socket, &QLocalSocket::readyRead, this, &AuthSession::readOutput);
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        readOutput();
        finish(m_succeeded);
    });

    // The service learns who is asking from the socket, and whom to
    // authenticate from this first line.
    m_socket->write(user.toUtf8() + '\n');

    return true;
}

void AuthSession::startProcess(const QString &user)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_channel = m_process;

    connect(m_process, &QProcess::readyReadStandardOutput, this, &AuthSession::readOutput);
    connect(m_process, &QProcess::finished, this, [this] {
        readOutput();
        finish(m_succeeded);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "Cannot start" << helperPath << m_process->errorString();
            finish(false);
        }
    });

    m_process->start(helperPath, { user });
}

void AuthSession::readOutput()
{
    m_buffer.append(m_channel->readAll());

    int newline;

    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);

        processLine(line);
    }
}

void AuthSession::processLine(const QByteArray &line)
{
    static const QByteArray echoOff = QByteArrayLiteral("PAM_PROMPT_ECHO_OFF ");
    static const QByteArray echoOn = QByteArrayLiteral("PAM_PROMPT_ECHO_ON ");
    static const QByteArray errorMessage = QByteArrayLiteral("PAM_ERROR_MSG ");
    static const QByteArray textInfo = QByteArrayLiteral("PAM_TEXT_INFO ");

    if (line.startsWith(echoOff)) {
        emit request(unescape(line.mid(echoOff.size())), false);
    } else if (line.startsWith(echoOn)) {
        emit request(unescape(line.mid(echoOn.size())), true);
    } else if (line.startsWith(errorMessage)) {
        emit notice(unescape(line.mid(errorMessage.size())), true);
    } else if (line.startsWith(textInfo)) {
        emit notice(unescape(line.mid(textInfo.size())), false);
    } else if (line.startsWith("SUCCESS")) {
        // Reported once the helper exits, a moment after this line.
        m_succeeded = true;
    } else if (line.startsWith("FAILURE")) {
        m_succeeded = false;
    } else if (line.startsWith("polkit-agent-helper-1: ")) {
        // The socket-activated helper may send its stderr down the socket too.
        qInfo().noquote() << line;
    } else {
        qWarning() << "Unknown line from the polkit helper" << line;
    }
}

void AuthSession::finish(bool gainedAuthorization)
{
    if (m_finished)
        return;

    m_finished = true;

    emit completed(gainedAuthorization);
}
