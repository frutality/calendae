#ifndef RECORDINGAPI_H
#define RECORDINGAPI_H

#include "calendar/googlecalendarapi.h"

#include <QList>
#include <QString>

// Stands in for GoogleCalendarApi: records every fetchEvents() call and lets
// the test deliver replies on demand, with no network or auth involved.
// Shared by test_montheventstore.cpp, test_storebackedeventscontroller.cpp
// and test_reminderscheduler.cpp.
class RecordingApi : public GoogleCalendarApi
{
public:
    using GoogleCalendarApi::GoogleCalendarApi;

    struct Call
    {
        QString calendarId;
        QString timeMin;
        QString timeMax;
        quint64 id;
    };
    QList<Call> calls;
    quint64 nextId = 1;

    quint64 fetchEvents(const QString &calendarId, const QString &timeMin, const QString &timeMax) override
    {
        const quint64 id = nextId++;
        calls.append({calendarId, timeMin, timeMax, id});
        return id;
    }

    void deliver(quint64 id, const QString &calendarId, const QList<Event> &events)
    {
        emit eventsFetched(id, calendarId, events);
    }
    void failRequest(quint64 id, const QString &calendarId, const QString &message, bool transient = false)
    {
        emit eventsFetchFailed(id, calendarId, message, transient);
    }
};

#endif // RECORDINGAPI_H
