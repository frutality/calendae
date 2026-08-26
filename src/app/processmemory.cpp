#include "processmemory.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif

qint64 currentProcessResidentMemoryBytes()
{
#if defined(Q_OS_LINUX)
    QFile statusFile(QStringLiteral("/proc/self/status"));
    if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    QTextStream stream(&statusFile);
    QString line;
    while (stream.readLineInto(&line)) {
        if (!line.startsWith(QStringLiteral("VmRSS:")))
            continue;
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            return parts.at(1).toLongLong() * 1024;
        return 0;
    }
    return 0;
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<qint64>(counters.WorkingSetSize);
    return 0;
#elif defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<qint64>(info.resident_size);
    return 0;
#else
    return 0;
#endif
}

QString formatMemorySize(qint64 bytes)
{
    constexpr qint64 kKiB = 1024;
    constexpr qint64 kMiB = kKiB * 1024;
    constexpr qint64 kGiB = kMiB * 1024;

    if (bytes >= kGiB)
        return QStringLiteral("%1 GB").arg(qRound(static_cast<double>(bytes) / kGiB));
    if (bytes >= kMiB)
        return QStringLiteral("%1 MB").arg(qRound(static_cast<double>(bytes) / kMiB));
    if (bytes >= kKiB)
        return QStringLiteral("%1 KB").arg(qRound(static_cast<double>(bytes) / kKiB));
    return QStringLiteral("%1 B").arg(bytes);
}
