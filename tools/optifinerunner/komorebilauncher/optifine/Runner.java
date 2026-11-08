// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Komorebi Lite - Minecraft Launcher
 *  Copyright (C) 2026 KomorebiLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

/*
 * Headless entry point for OptiFine's own installer.
 *
 * Source: the approach is the one Prism Launcher takes (its OptiFine install task runs a small
 * helper class so the installer never shows its window). This file is written for this project.
 *
 * OptiFine's installer is the only official patcher, so reproducing its output by hand would drift
 * with every OptiFine generation. Its entry point is not stable across those generations: modern
 * jars expose doInstall(File), while older ones only have doInstall() and locate .minecraft from the
 * environment. Both are tried here, so a single runner covers every build the mirror lists.
 */
package komorebilauncher.optifine;

import java.io.File;
import java.lang.reflect.Method;

public final class Runner {
    private Runner() {}

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException("usage: Runner <minecraft-directory>");
        }
        File minecraftDir = new File(args[0]);

        Class<?> installer = Class.forName("optifine.Installer");
        try {
            Method withDirectory = installer.getMethod("doInstall", File.class);
            withDirectory.invoke(null, minecraftDir);
            return;
        } catch (NoSuchMethodException ignored) {
            // Older build: the target directory comes from the environment instead.
        }
        installer.getMethod("doInstall").invoke(null);
    }
}
