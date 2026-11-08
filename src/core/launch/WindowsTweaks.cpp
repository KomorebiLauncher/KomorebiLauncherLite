// SPDX-License-Identifier: GPL-3.0-only
#include "core/launch/WindowsTweaks.h"

#include <QDir>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace Komorebi {

bool setGpuPreference(const QString& executable, bool high)
{
#if defined(Q_OS_WIN)
    if (executable.isEmpty())
        return false;

    // The value name is the full path of the program; the data is "GpuPreference=<n>;", where 2 is
    // the high performance adapter and 0 is "let Windows decide".
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }

    const QString path = QDir::toNativeSeparators(executable);
    const QString value = high ? QStringLiteral("GpuPreference=2;") : QStringLiteral("GpuPreference=0;");
    const std::wstring name = path.toStdWString();
    const std::wstring data = value.toStdWString();

    const LSTATUS status = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(data.c_str()),
                                          static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    Q_UNUSED(executable);
    Q_UNUSED(high);
    return false;
#endif
}

bool setProcessPriority(qint64 processId, const QString& level)
{
#if defined(Q_OS_WIN)
    if (processId <= 0 || level.isEmpty())
        return false;

    DWORD priority = NORMAL_PRIORITY_CLASS;
    if (level == QLatin1String("high"))
        priority = HIGH_PRIORITY_CLASS;
    else if (level == QLatin1String("above"))
        priority = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (level == QLatin1String("below"))
        priority = BELOW_NORMAL_PRIORITY_CLASS;
    else if (level == QLatin1String("low"))
        priority = IDLE_PRIORITY_CLASS;

    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (!process)
        return false;
    const bool ok = SetPriorityClass(process, priority) != 0;
    CloseHandle(process);
    return ok;
#else
    Q_UNUSED(processId);
    Q_UNUSED(level);
    return false;
#endif
}

}  // namespace Komorebi
