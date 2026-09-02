#ifndef KEYCHAINJOB_H
#define KEYCHAINJOB_H

#include "credentialsprovider.h" // CredentialsProvider::keychainService

#include <keychain.h>

#include <QObject>
#include <QString>

// Single construction point for every QtKeychain job in the app, so the
// storage policy lives in exactly one place.
//
// Layered storage:
//   1. A real OS keyring reached through the Secret Service API (QtKeychain's
//      libsecret backend: gnome-keyring, KeePassXC, KWallet 6, pass-secret-
//      service, ...) or KWallet's own D-Bus protocol. QtKeychain always
//      prefers this whenever a backend is reachable, and transparently
//      migrates any pre-existing plain-text entry into it.
//   2. setInsecureFallback(true): on a machine with *no* keyring daemon at
//      all, QtKeychain persists the value in an on-disk QSettings file
//      (~/.config/<service>.conf on Linux) instead of failing outright. This
//      is a deliberate trade -- an unencrypted refresh token on a single-user
//      desktop beats forcing a fresh browser sign-in on every launch.
//      keychainSecretStoredInsecurely() reports whether this happened so the
//      UI can warn about the downgrade.
template <typename Job>
Job *makeKeychainJob(const QString &key, QObject *parent = nullptr)
{
    auto *job = new Job(CredentialsProvider::keychainService, parent);
    job->setKey(key);
    job->setInsecureFallback(true);
    return job;
}

// True once any secret has actually landed in the plain-text fallback store,
// i.e. no OS keyring was available at write time. One cheap QSettings lookup;
// safe to call on every sign-in.
bool keychainSecretStoredInsecurely();

#endif // KEYCHAINJOB_H
