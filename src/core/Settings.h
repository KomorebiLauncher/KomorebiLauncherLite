// SPDX-License-Identifier: GPL-3.0-only
/*
 *  What a player can change about the launcher, kept in one small file next to the game.
 *
 *  Everything has a sensible default, so a missing or broken file is not an error: the launcher
 *  simply starts with what most people want.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Komorebi {

struct Settings {
    // appearance
    QString language = QStringLiteral("auto");  ///< "auto", "zh_CN", "en", "ja"
    bool darkTheme = false;
    int wallpaperOpacity = 100;  ///< 0-100, how much of the wallpaper shows through

    // content
    QString curseForgeApiKey;  ///< empty: the key the project ships with

    // downloads
    QString downloadSite = QStringLiteral("official-first");  ///< official-first, mirror-first, official-only, mirror-only
    QString mirrorBase = QStringLiteral("https://bmclapi2.bangbang93.com");
    int maxConcurrency = 8;
    int speedLimitKb = 0;  ///< 0: no limit

    // launching
    int memoryMb = 2048;
    bool lockMemory = false;  ///< also pass -Xms, so the heap is reserved up front
    int width = 0;            ///< 0: let the game pick
    int height = 0;
    QString javaPath;      ///< empty: choose by version
    QString jvmArguments;  ///< extra JVM arguments, space separated
    QString gameArguments;  ///< extra game arguments
    QString preLaunchCommand;
    bool highPerformanceGpu = false;
    bool legacyFix = false;
    bool unsafeAgent = true;
    QString processPriority;  ///< empty, "high", "above", "below"

    static Settings load();
    void save() const;

    /// The arguments as a list, quotes respected.
    static QStringList splitArguments(const QString& text);
};

}  // namespace Komorebi
