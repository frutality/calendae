#ifndef PROCESSMEMORY_H
#define PROCESSMEMORY_H

#include <QString>

// Resident set size of the current process, in bytes. Returns 0 if it
// couldn't be determined on this platform.
qint64 currentProcessResidentMemoryBytes();

// e.g. "32 MB". Deliberately terse: a number and a unit, nothing else.
QString formatMemorySize(qint64 bytes);

#endif // PROCESSMEMORY_H
