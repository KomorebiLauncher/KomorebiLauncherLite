# Komorebi Lite

轻量级 Minecraft 启动器，对标 PCL：只做普通玩家真正用到的功能，安装 → 选版本 → 启动，路径短。

这是独立项目，不是 [KomorebiLauncher](https://github.com/KomorebiLauncher/KomorebiLauncher) 的分支：两者代码库、数据目录、发行包都各自独立。

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

本项目 GPL-3.0-only，见 [LICENSE](LICENSE)。标志与界面为原创；下列部分的实现参考了同一组织的另一个项目，二者同为 GPL-3.0：

| 部分 | 来源 | 说明 |
| --- | --- | --- |
| 微软账户登录（设备码 → Xbox Live → XSTS → Minecraft Services） | [KomorebiLauncher](https://github.com/KomorebiLauncher/KomorebiLauncher) | 其本身 fork 自 [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher)（GPL-3.0），协议实现源自 Prism Launcher 贡献者 |
| CurseForge 整合包解析与下载流程 | 同上 | 同上 |
| 版本清单、库规则、natives 约定 | Mojang 公开的清单格式 | 事实性数据格式，非代码 |

这些部分在本项目中按本项目的结构重写，未逐行复制；若后续直接搬用文件，将保留文件头部的原版权声明并注明修改。完整声明见 [NOTICE.md](NOTICE.md)。

Minecraft 是 Mojang Studios 的商标。本项目非 Minecraft 官方产品，未经 Mojang 或 Microsoft 批准，也与其无隶属关系；本仓库不分发任何游戏文件。

## 许可

GPL-3.0-only，见 [LICENSE](LICENSE)。
