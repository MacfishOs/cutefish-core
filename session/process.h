// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROCESS_H
#define PROCESS_H

#include <QProcess>

class Process : public QProcess
{
    Q_OBJECT

public:
    Process(QObject* parent = nullptr);
    ~Process();
};

#endif
