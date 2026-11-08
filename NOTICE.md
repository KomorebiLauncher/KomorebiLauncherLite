# Third-party notices

Komorebi Lite is licensed under GPL-3.0-only (see [LICENSE](LICENSE)). This file records where the
parts of it that were not written from scratch come from, as the licence asks.

## KomorebiLauncher / Prism Launcher

Microsoft account sign-in and CurseForge modpack handling in this launcher follow the protocol work
done in [KomorebiLauncher](https://github.com/KomorebiLauncher/KomorebiLauncher), a fork of
[Prism Launcher](https://github.com/PrismLauncher/PrismLauncher).

- Prism Launcher: Copyright (C) 2022–2025 Prism Launcher contributors, and the MultiMC contributors
  before them. GPL-3.0-only.
- KomorebiLauncher: Copyright (C) 2025–2026 KomorebiLauncher contributors. GPL-3.0-only, as a fork
  of the above.

The code here is written against the same public endpoints and the same account model, in this
project's own structure. Where a file is taken from either project rather than re-derived, it keeps
the original copyright header, and the modification is noted in the header and in the commit that
brought it in.

## Qt

Qt 6 (Widgets, Svg, Network, LinguistTools), Copyright (C) The Qt Company Ltd. and contributors,
used under LGPL-3.0 / GPL-3.0 as shipped with the build.

## Minecraft metadata

Version manifests, library rules and asset indexes are data formats published by Mojang Studios; the
launcher reads them, and downloads their contents from Mojang or from a mirror. No game files are
distributed by this repository.

## Not affiliated

Minecraft is a trademark of Mojang Studios. This is not an official Minecraft product; it is not
approved by or associated with Mojang or Microsoft.
