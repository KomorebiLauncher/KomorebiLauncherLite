// SPDX-License-Identifier: GPL-3.0-only
/*
 *  One launcher per data directory.
 *
 *  Two copies started from the same folder write the same settings, the same accounts and the same
 *  instance files, and on top of that they hand the same game directory to two Minecraft processes.
 *  A lock file keeps the second copy out, and the named pipe next to it lets the second copy ask the
 *  first one to show itself, so starting the launcher twice looks like the window coming forward
 *  rather than like nothing happening at all.
 */

#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QLocalServer;
class QLockFile;

namespace Komorebi {

class SingleInstance : public QObject {
    Q_OBJECT

   public:
    /// `dataRoot` decides which launchers count as the same one.
    explicit SingleInstance(const QString& dataRoot);
    ~SingleInstance() override;

    /**
     * Takes the lock for this data directory.
     *
     * False means another launcher already holds it, and that launcher should be raised with
     * `raiseRunning()` before this process exits.
     */
    bool claim();

    /// Asks the launcher that holds the lock to show its window; false when it did not answer.
    bool raiseRunning();

    /// Starts answering other starts; `raise` runs when one of them asks. False when it could not.
    bool listen(const std::function<void()>& raise);

   private:
    QString m_name;
    QLockFile* m_lock = nullptr;
    QLocalServer* m_server = nullptr;
};

}  // namespace Komorebi
