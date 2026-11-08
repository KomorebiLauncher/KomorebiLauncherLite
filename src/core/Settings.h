// SPDX-License-Identifier: GPL-3.0-only
/*
 *  What a player can change about the launcher, kept in one small file next to the game.
 *
 *  Everything has a sensible default, so a missing or broken file is not an error: the launcher
 *  simply starts with what most people want.
 */

#pragma once

#include <QString>

namespace Komorebi {

struct Settings {
    QString language = QStringLiteral("auto");  ///< "auto", "zh_CN", "en", "ja"
    int memoryMb = 2048;                        ///< the maximum heap handed to the game
    QString javaPath;                           ///< empty: let the launcher choose by version

    static Settings load();
    void save() const;
};

}  // namespace Komorebi
