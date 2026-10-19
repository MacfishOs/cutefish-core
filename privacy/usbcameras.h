// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

// USB video class interfaces. An unauthorized interface has no driver, so its
// /dev/video nodes and every stream on them are gone until it is authorized.
class UsbCameras
{
public:
    explicit UsbCameras(const std::string &sysfs = "/sys");

    void setAuthorized(bool authorized);

private:
    std::string m_sysfs;
};
