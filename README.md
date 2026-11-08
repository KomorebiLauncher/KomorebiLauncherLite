# Komorebi Lite

轻量级 Minecraft 启动器，对标 PCL：只做普通玩家真正用到的功能，安装 → 选版本 → 启动，路径短。

本仓库是 KomorebiLauncher 的**轻量版**：[完整版](https://github.com/KomorebiLauncher/KomorebiLauncher)（Prism Launcher 的 fork）与本版是同一个项目的两个版本，两个仓库、两套数据目录、两个发行包，功能面各自取舍——完整版面向需要更多功能与整合包生态的玩家，本版只保留普通玩家真正用到的部分。

完整版是 [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) 的 fork，因此本版沿用的账号登录、整合包处理等逻辑，其源头是 Prism Launcher 及其前身 MultiMC 的贡献者（GPL-3.0）。归属写明在 [NOTICE.md](NOTICE.md) 与启动器的「关于」页。

## 现在的状态

| 能力 | 状态 |
| --- | --- |
| 安装原版（官方清单 → 客户端 jar / 库 / 资源 / 资源索引） | 可用，`--install <版本>` |
| 安装 Fabric / Quilt / Forge | 可用，`--install <版本> --loader fabric\|quilt\|forge` |
| 启动已安装版本 | 可用，`--launch-version <版本>` |
| 界面（启动 / Java / 日志 / 关于 / 设置） | 可用 |
| 三语言（简体中文 / English / 日本語） | 可用 |
| 内存、窗口、JVM 参数、游戏参数、启动前命令、Java 指定 | 可用 |
| 下载源（官方 / 镜像）、并发数、限速 | 可用 |
| 微软账户登录 | 未做 |
| CurseForge 整合包下载 | 未做 |
| NeoForge、OptiFine | 未做 |
| Mod 管理、存档、截图 | 未做 |

## 构建

需要 Qt 6（Widgets / Svg / Network）与 MSVC 2022，不需要 vcpkg。

```powershell
build.bat            # 配置 + 全量编译到 build\
run.bat              # 打开窗口
```

Qt 路径在 `build.bat` 与 `run.bat` 顶部，换机器时改那两行。`build.bat` 故意每次都全量编译：增量编译保留过旧的 .obj 会在启动时崩溃，排查成本远高于多等一分钟。

## 目录布局

按游戏自己的约定，装出来的目录可以被任何启动器直接使用：

```
%APPDATA%\KomorebiLauncherLite\
  versions\<id>\<id>.json     这一版的描述
  versions\<id>\<id>.jar      游戏本体
  versions\<id>\natives\      需要解包的原生库
  libraries\                  各版本共用
  assets\indexes\             资源清单
  assets\objects\             资源本体
  settings.json               启动器设置
  logs\                       每次启动的游戏日志
```

设置 `KOMOREBI_DATA` 可以换数据目录（测试用）。

## 诊断入口

```
run.bat --selftest                              渲染自检，结果写 build\selftest.log
run.bat --install 1.20.1                        安装一个版本，过程写 install.log
run.bat --install 1.20.1 --loader forge         装完再装加载器
run.bat --launch-version 1.20.1                 启动，输出写 launch.log
run.bat --launch-plan <版本 json>               只打印命令行
run.bat --resolve <版本 id>                     打印继承链与合并结果
```

## 来源与致谢

本项目 GPL-3.0-only，见 [LICENSE](LICENSE)。标志与界面为原创。

代码的谱系是：MultiMC → Prism Launcher → KomorebiLauncher（完整版）→ 本版。因此下列部分虽然在本仓库中按本版结构实现，其设计源头是上游项目，归属属于其贡献者：

| 部分 | 上游源头 |
| --- | --- |
| 微软账户登录（设备码 → Xbox Live → XSTS → Minecraft Services） | Prism Launcher 及其前身 MultiMC 的贡献者 |
| CurseForge 整合包解析与下载流程 | 同上 |
| 版本清单、库规则、natives 约定 | Mojang 公开的清单格式（事实性数据，非代码） |

Copyright (C) 2022–2025 Prism Launcher contributors，以及更早的 MultiMC contributors；Copyright (C) 2025–2026 KomorebiLauncher contributors。均以 GPL-3.0-only 授权。

Minecraft 是 Mojang Studios 的商标。本项目非 Minecraft 官方产品，未经 Mojang 或 Microsoft 批准，也与其无隶属关系；本仓库不分发任何游戏文件。
## 许可

GPL-3.0-only，见 [LICENSE](LICENSE)。
