#pragma once

#include <QProcess>

class ShellProcess final : public QProcess
{
public:
    explicit ShellProcess(QObject *parent = nullptr);
    ~ShellProcess() override;

    void startForSession(const QString &program, const QString &socket);
    void stop();
};
