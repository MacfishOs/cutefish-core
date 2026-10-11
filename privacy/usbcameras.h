// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// USB video class interfaces. An unauthorized interface has no driver, so its
// /dev/video nodes and every stream on them are gone until it is authorized.
class UsbCameras
{
public:
    explicit UsbCameras(const QString &sysfs = QStringLiteral("/sys"));

    void setAuthorized(bool authorized);

private:
    QString m_sysfs;
};
