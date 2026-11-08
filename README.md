# Komorebi Lite

轻量级 Minecraft 启动器，对标 PCL：只做普通玩家真正用到的功能，安装 → 选版本 → 启动，路径短。

本仓库是 KomorebiLauncher 的**轻量版**：[完整版](https://github.com/KomorebiLauncher/KomorebiLauncher)（Prism Launcher 的 fork）与本版是同一个项目的两个版本，两个仓库、两套数据目录、两个发行包，功能面各自取舍——完整版面向需要更多功能与整合包生态的玩家，本版只保留普通玩家真正用到的部分。

完整版是 [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) 的 fork。**本版代码是独立实现的**：账号登录、版本安装、启动参数构建等均是照公开协议与公开格式重新写的，没有引入完整版或上游的源码。**两处有明确的外部来源**（NeoForge 的组件元数据由 Prism 发布、OptiFine 的无窗口安装沿用 Prism 的做法），都只说明来源，不表示与 Prism 有从属关系。归属写明在 [NOTICE.md](NOTICE.md) 与启动器的「关于」页。

## 现在的状态

| 能力 | 状态 |
| --- | --- |
| 安装原版（官方清单 → 客户端 jar / 库 / 资源 / 资源索引） | 可用，`--install <版本>` |
| 安装 Fabric / Quilt / Forge / NeoForge / OptiFine | 可用，`--install <版本> --loader fabric\|quilt\|forge\|neoforge\|optifine` |
| 启动已安装版本 | 可用，`--launch-version <版本>` |
| 界面（启动 / 整合包 / 内容 / Java / 账户 / 日志 / 工具 / 关于 / 设置） | 可用 |
| 三语言（简体中文 / English / 日本語） | 可用 |
| 内存（含锁定 `-Xms`）、窗口、JVM 参数、游戏参数、启动前命令、Java 指定 | 可用 |
| Java 运行时按版本自动下载（Mojang 运行时，装进数据目录的 `java/`） | 可用 |
| 高性能显卡（写 Windows 图形首选项）、进程优先级、原生库解包 | 可用 |
| 下载源（官方 / 镜像 / 仅官方 / 仅镜像，可自定义镜像）、并发数、限速 | 可用 |
| 实例级设置（内存 / 参数 / 备注按版本覆盖） | 可用 |
| 微软账户登录（设备码），启动时带正版档案 | 可用 |
| CurseForge 整合包（搜索 + 安装）、本地整合包导入 | 可用 |
| 内容管理（模组 / 资源包 / 光影 / 存档 / 截图） | 可用 |
| 工具页（清理垃圾、桌面快捷方式、导入导出设置、下载文件） | 可用 |

## 构建

需要 Qt 6（Widgets / Svg / Network）与 MSVC 2022，不需要 vcpkg。

Lite 用**自己的一份 Qt**，默认装在 `%LOCALAPPDATA%\KomorebiLauncherLite\Qt\6.10.3\msvc2022_64`，与完整版那份互不共用；路径写在 `build.bat` 与 `run.bat` 顶部，换机器时改那两行。每次 push 由 CI 做一次构建校验（`.github/workflows/build.yml`，Windows + Qt 6.10.3 + offscreen 渲染自检），本地构建只在需要验证运行时才做。

```powershell
build.bat            # 配置 + 全量编译到 build\
run.bat              # 打开窗口
```

`build.bat` 故意每次都全量编译：增量编译保留过旧的 .obj 会在启动时崩溃，排查成本远高于多等一分钟。

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
run.bat --install 1.20.1 --loader forge         装完再装加载器（fabric / quilt / forge / neoforge / optifine）
run.bat --loader-components net.neoforged 26.3  只用组件元数据装加载器（调试用）
run.bat --launch-version 1.20.1                 启动，输出写 launch.log
run.bat --launch-plan <版本 json>               只打印命令行
run.bat --resolve <版本 id>                     打印继承链与合并结果
run.bat --fetch <地址> <文件>                   用启动器自己的下载层取一个文件，结果写 fetch.log
run.bat --unpack <压缩包> <目录>                用启动器自己的解包器展开一个压缩包
```

## 来源与致谢

本项目 GPL-3.0-only，见 [LICENSE](LICENSE)。标志与界面为原创，本仓库的代码为本项目独立实现。

代码的谱系是项目层面的：MultiMC → Prism Launcher → KomorebiLauncher（完整版）→ 本版。本版不包含这些项目的源码；实现时参照的是它们公开的行为与以下公开资料：

| 部分 | 依据 |
| --- | --- |
| 微软账户登录（设备码 → Xbox Live → XSTS → Minecraft Services） | Microsoft 与 Xbox Live 的公开 OAuth 与身份协议文档 |
| 版本清单、库规则、natives 约定 | Mojang 公开的清单格式 |
| 加载器安装 | Fabric / Quilt 的公开安装接口；Forge 调用其官方安装器的无界面模式 |
| NeoForge | Prism Launcher 公开发布的组件元数据（只当作数据源使用） |
| OptiFine 的无窗口安装 | Prism Launcher 的做法：用一个几行的 Java 助手调用 OptiFine 自己的安装器；本项目的 `tools/optifinerunner` 按此思路重写 |

上面两处是**来源**，不是从属关系：本项目与 Prism Launcher 没有隶属或背书关系。

Copyright (C) 2025–2026 KomorebiLauncher contributors。以 GPL-3.0-only 授权。

Minecraft 是 Mojang Studios 的商标。本项目非 Minecraft 官方产品，未经 Mojang 或 Microsoft 批准，也与其无隶属关系；本仓库不分发任何游戏文件。
## 许可

GPL-3.0-only，见 [LICENSE](LICENSE)。
