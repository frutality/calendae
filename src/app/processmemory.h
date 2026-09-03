#ifndef PROCESSMEMORY_H
#define PROCESSMEMORY_H

#include <QString>

// Resident set size of the current process, in bytes. Returns 0 if it
// couldn't be determined on this platform.
qint64 currentProcessResidentMemoryBytes();

// Proportional set size: RSS with every shared page counted only as this
// process's fraction of it (page / number of processes mapping it). The
// honest "what this process costs the machine" number — much lower than
// RSS for a Qt app, whose library pages are shared. Linux-only (parses
// /proc/self/smaps_rollup, kernel >= 4.14); returns 0 where unavailable.
qint64 currentProcessProportionalSetSizeBytes();

// e.g. "32 MB". Deliberately terse: a number and a unit, nothing else.
QString formatMemorySize(qint64 bytes);

#endif // PROCESSMEMORY_H
