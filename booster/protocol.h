// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BOOSTER_PROTOCOL_H
#define BOOSTER_PROTOCOL_H

#include <QDataStream>
#include <QIODevice>
#include <QStringList>

// What cutefish-appruntime and cutefish-booster say over their socket pair:
// one message per datagram.
namespace BoosterProtocol {

// Booster to runtime, single bytes.
constexpr char Ready = 'R';
constexpr char Accepted = 'A';
constexpr char Refused = 'F';

// Runtime to booster.
struct LaunchRequest {
    QString module;
    // Canonical path of the executable the request stands for.
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QString activationToken;
};

inline QByteArray encode(const LaunchRequest &request)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << request.module << request.executable << request.arguments
           << request.workingDirectory << request.activationToken;
    return data;
}

inline bool decode(const QByteArray &data, LaunchRequest *request)
{
    QDataStream stream(data);
    stream >> request->module >> request->executable >> request->arguments
           >> request->workingDirectory >> request->activationToken;
    return stream.status() == QDataStream::Ok && !request->module.isEmpty()
        && !request->executable.isEmpty() && !request->arguments.isEmpty();
}

} // namespace BoosterProtocol

#endif // BOOSTER_PROTOCOL_H
