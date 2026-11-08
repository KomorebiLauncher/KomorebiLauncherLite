// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The Java runtimes on this machine.
 *
 *  Nothing here is clever: the places people install Java are known, and each runtime carries a
 *  `release` file that says what it is. Reading that file is enough to pick a runtime by major
 *  version, which is all the launcher needs.
 */

#pragma once

#include <QList>
#include <QString>

namespace Komorebi {

struct JavaRuntime {
    QString path;      ///< the executable
    QString home;      ///< the runtime's directory
    int major = 0;     ///< 8, 17, 21, 25...
    QString version;   ///< the full version string from the release file
    QString vendor;
    QString arch;
    bool managed = false;  ///< lives in the launcher's own java directory
};

class JavaScan {
   public:
    /// Every runtime it can find, best first: the launcher's own, then the usual install places.
    static QList<JavaRuntime> find();

    /// The runtime for a version: the closest major to what it asks for.
    static JavaRuntime pick(const QList<JavaRuntime>& runtimes, int requiredMajor);

    /// Reads one runtime; an empty major when the path is not a Java.
    static JavaRuntime inspect(const QString& executable);
};

}  // namespace Komorebi
