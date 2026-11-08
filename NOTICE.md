# Third-party notices

Komorebi Lite is the light edition of KomorebiLauncher, and both are licensed under GPL-3.0-only
(see [LICENSE](LICENSE)). This file records where the parts that did not originate in this project
come from, as the licence asks.

## Lineage

```
MultiMC → Prism Launcher → KomorebiLauncher (full edition) → Komorebi Lite (this repository)
```

The full edition is a fork of Prism Launcher; this edition is the same project with a narrower
feature set. Code carried across the two editions is the project's own; what it descends from is
Prism Launcher, and before it MultiMC.

- MultiMC: Copyright (C) 2013–2022 MultiMC contributors.
- Prism Launcher: Copyright (C) 2022–2025 Prism Launcher contributors.
- KomorebiLauncher / Komorebi Lite: Copyright (C) 2025–2026 KomorebiLauncher contributors.

All of the above are distributed under GPL-3.0-only.

Where a file is taken from the full edition rather than written for this one, it keeps the original
copyright header, and the modification is noted in the header and in the commit that brought it in.

## What came from upstream

| Part | Upstream origin |
| --- | --- |
| Microsoft account sign-in (device code → Xbox Live → XSTS → Minecraft Services) | Prism Launcher implementation, Prism Launcher and MultiMC contributors |
| CurseForge modpack parsing and download flow | same |
| Version manifests, library rules, native library conventions | data formats published by Mojang Studios |

## Qt

Qt 6 (Widgets, Svg, Network, LinguistTools), Copyright (C) The Qt Company Ltd. and contributors,
used under LGPL-3.0 / GPL-3.0 as shipped with the build.

## Not affiliated

Minecraft is a trademark of Mojang Studios. This is not an official Minecraft product; it is not
approved by or associated with Mojang or Microsoft. No game files are distributed by this
repository: versions, assets and libraries are downloaded from Mojang or from a mirror.
