#include "keychainbackend.h"
#include "keychainjob.h"

#include <keychain.h>

namespace {
void connectFinished(QKeychain::Job *job, QObject *parent, const std::function<void(QKeychain::Job *)> &onFinished)
{
    // See keychainbackend.h: parent doubles as the connection context so the
    // callback safely never fires once the caller it belongs to is gone;
    // nullptr means the caller explicitly wants fire-and-forget instead.
    if (parent)
        QObject::connect(job, &QKeychain::Job::finished, parent, onFinished);
    else
        QObject::connect(job, &QKeychain::Job::finished, onFinished);
}
} // namespace

void RealKeychainBackend::read(const QString &key, QObject *parent,
                                const std::function<void(QKeychain::Error, const QString &)> &callback)
{
    auto *job = makeKeychainJob<QKeychain::ReadPasswordJob>(key, parent);
    connectFinished(job, parent, [callback](QKeychain::Job *job) {
        auto *readJob = qobject_cast<QKeychain::ReadPasswordJob *>(job);
        callback(readJob->error(), readJob->textData());
    });
    job->start();
}

void RealKeychainBackend::write(const QString &key, const QString &textData, QObject *parent,
                                 const std::function<void(QKeychain::Error)> &callback)
{
    auto *job = makeKeychainJob<QKeychain::WritePasswordJob>(key, parent);
    job->setTextData(textData);
    connectFinished(job, parent, [callback](QKeychain::Job *job) { callback(job->error()); });
    job->start();
}

void RealKeychainBackend::remove(const QString &key, QObject *parent,
                                  const std::function<void(QKeychain::Error)> &callback)
{
    auto *job = makeKeychainJob<QKeychain::DeletePasswordJob>(key, parent);
    connectFinished(job, parent, [callback](QKeychain::Job *job) { callback(job->error()); });
    job->start();
}
