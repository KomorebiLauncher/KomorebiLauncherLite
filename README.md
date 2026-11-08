# Komorebi Lite

轻量级 Minecraft 启动器，对标 PCL：只做普通玩家真正用到的功能，安装 → 选版本 → 启动，路径短。

这是独立项目，不是 [KomorebiLauncher](https://github.com/KomorebiLauncher/KomorebiLauncher) 的分支：两者代码库、数据目录、发行包都各自独立。

## 现在的状态

| 能力 | 状态 |
| --- | --- |
| 安装原版（官方清单 → 客户端 jar / 库 / 资源 / 资源索引） | 可用，`komorebi-lite --install <版本>` |
| 启动已安装版本 | 可用，`komorebi-lite --launch-version <版本>` |
| 窗口（实例列表、主题、控件） | 可用，界面正在按 Lite 的形态重建 |
| Java 选择（按版本要求挑运行时） | 可用 |
| 账号（离线 / 微软） | 未做 |
| 加载器（Forge / NeoForge / Fabric / Quilt / OptiFine） | 未做 |
| Mod、整合包导入、存档 | 未做 |
| 设置页 | 未做 |
| 语言（zh_CN / en / ja） | 未做 |

## 构建

需要 Qt 6（Widgets / Svg / Network）与 MSVC 2022，不需要 vcpkg。

```powershell
build.bat            # 配置 + 编译到 build\
run.bat              # 打开窗口
```

Qt 路径在 `build.bat` 与 `run.bat` 顶部，换机器时改那两行。

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
```

设置 `KOMOREBI_DATA` 可以换数据目录（测试用）。

## 诊断入口

```
run.bat --selftest                              渲染自检，结果写 build\selftest.log
run.bat --install 1.20.1                        安装一个版本，过程写 install.log
run.bat --launch-version 1.20.1                 启动，输出写 launch.log
run.bat --launch-plan <版本 json>               只打印命令行
```

## 许可

GPL-3.0，见 [LICENSE](LICENSE)。
