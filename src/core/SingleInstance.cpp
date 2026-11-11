// SPDX-License-Identifier: GPL-3.0-only
#include "core/SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>

namespace Komorebi {

namespace {

/// Where the lock and the pipe name come from: two starts are the same launcher when they share this.
QString instanceName(const QString& dataRoot)
{
    // Windows paths are not case sensitive, so "D:\MC" and "d:\mc" are the same folder and must be
    // the same launcher. The name is short so the whole thing fits in a pipe name.
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dataRoot)).toLower();
    const QByteArray digest = QCryptographicHash::hash(clean.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("komorebi-lite-") + QString::fromLatin1(digest);
}

QString lockDirectory()
{
    // The data directory can be on a drive that is gone, or one the player cannot write to, and the
    // lock has to work anyway: it lives with the other per-user state instead.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    return QDir(base).filePath(QStringLiteral("KomorebiLite"));
}

}  // namespace

SingleInstance::SingleInstance(const QString& dataRoot)
    : m_name(instanceName(dataRoot))
{
}

SingleInstance::~SingleInstance()
{
    delete m_lock;
}

bool SingleInstance::claim()
{
    if (m_lock != nullptr)
        return true;

    const QDir directory(lockDirectory());
    if (!directory.exists() && !QDir().mkpath(directory.path()))
        return true;  // No place for a lock is no reason to refuse to start.

    m_lock = new QLockFile(QDir(directory).filePath(m_name + QStringLiteral(".lock")));
    // A lock left behind by a crash is taken over rather than blocking the launcher forever.
    m_lock->setStaleLockTime(30000);

    if (m_lock->tryLock(0))
        return true;

    if (m_lock->error() == QLockFile::LockFailedError)
        return false;

    // An unreadable lock file is not a running launcher; carry on and let the file sort itself out.
    delete m_lock;
    m_lock = nullptr;
    return true;
}

bool SingleInstance::raiseRunning()
{
    // The other launcher may still be opening its window, so give it a moment to answer: without it a
    // fast double click would look like nothing happened at all.
    for (int attempt = 0; attempt < 15; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(m_name);
        if (socket.waitForConnected(200)) {
            socket.write("raise\n");
            socket.flush();
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
            return true;
        }
        socket.abort();
        QThread::msleep(100);
    }
    return false;
}

bool SingleInstance::listen(const std::function<void()>& raise)
{
    if (m_lock == nullptr)
        return false;

    // A pipe left behind by a run that crashed would keep this one from starting.
    QLocalServer::removeServer(m_name);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_name)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this, raise] {
        while (QLocalSocket* socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
            socket->disconnectFromServer();
            if (raise)
                raise();
        }
    });
    return true;
}

}  // namespace Komorebi
