#ifndef STOREBACKEDEVENTSCONTROLLER_H
#define STOREBACKEDEVENTSCONTROLLER_H

#include "calendar.h"
#include "event.h"
#include "eventscontroller.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <optional>

class AuthManager;
class EventGridView;
class MonthEventStore;

// Shared implementation for the two view controllers — the 42-day month
// grid and the week/day time grid. Owns which calendars are enabled and
// turns the shared MonthEventStore's cached buckets into the view's
// per-calendar display lists; holds no events or fetch bookkeeping of its
// own. The only per-view differences are which calendar month(s) the
// visible range needs loaded (currentMonthKeys()) and which navigation
// signal feeds reloadCurrentRange() — a concrete subclass supplies both.
class StoreBackedEventsController : public EventsController
{
    Q_OBJECT
public:
    StoreBackedEventsController(AuthManager *authManager, MonthEventStore *store,
                                EventGridView *view, QObject *parent = nullptr);

public slots:
    void setCalendars(const QList<Calendar> &calendars) override;
    void setCalendarEnabled(const QString &calendarId, bool enabled) override;
    void clear() override;
    void refreshCalendar(const QString &calendarId) override;
    void refreshVisibleFromServer() override;

public:
    std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const override;

protected:
    bool ready() const;

    // The one genuinely view-specific query: the (first-of-)month keys the
    // currently displayed range needs the store to keep loaded.
    virtual QList<QDate> currentMonthKeys() const = 0;

    // A concrete subclass connects its view's navigation signal to this.
    void reloadCurrentRange();

    AuthManager *m_authManager;
    MonthEventStore *m_store;
    EventGridView *m_view;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;

private slots:
    void onBucketUpdated(const QDate &monthKey, const QString &calendarId);
    void onBucketFetchFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);
    void onBucketRefreshFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);

private:
    void renderCalendarFromCache(const QString &calendarId);
    void maybeEmitCycleFinished();
    QString calendarDisplayName(const QString &calendarId) const;
};

#endif // STOREBACKEDEVENTSCONTROLLER_H
