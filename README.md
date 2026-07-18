# MythwareHacker

> 集大成应用：整合 [JiYuTrainer](https://github.com/imengyu/JiYuTrainer) + [MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) + MythwareHide 三项目核心功能
>
> 极域电子教室 + 学生机房管理助手 综合对抗工具包

**当前版本：v1.0.0**

---

## 目录

- [功能](#功能)
- [系统要求](#系统要求)
- [编译方法](#编译方法)
- [使用教程](#使用教程)
- [项目结构](#项目结构)
- [技术原理](#技术原理)
- [致谢](#致谢)
- [许可](#许可)

---

## 功能

### 1. 窗口隐蔽（防截屏）

核心功能：让指定窗口对极域教师端"隐身"。

四套方案按优先级递降，自动选择：

| 方案 | API | 适用 | 说明 |
|------|-----|------|------|
| ① WDA | `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` | Win10 2004+ 本进程 | 截图时该窗口显示为黑块 |
| ② 注入 | `CreateRemoteThread` + DLL 注入跨进程调 WDA | 位数匹配的跨进程 | 通过偏移计算远程函数地址 |
| ③ Cloak | `DwmSetWindowAttribute(DWMWA_CLOAK)` | Win8+ 本进程 | DWM 层隐藏 |
| ④ 屏幕外 | `SetWindowPlacement` 移到屏幕外 | 兜底，全系统兼容 | 最可靠 |

**隐蔽效果**：教师端监控/截屏时看不到该窗口，且被它挡住的窗口在教师端**也能看见**（穿透）。

### 2. 极域进程控制

- **杀进程**：不依赖 `taskkill`/`ntsd`，用 `OpenProcess` + `TerminateProcess`，备用 `DebugActiveProcessStop`
- **挂起/恢复**：枚举进程线程，`SuspendThread`/`ResumeThread`
- **广播窗口化**：查找极域全屏广播窗口，改为 `WS_OVERLAPPEDWINDOW` 窗口模式
- **广播全屏化**：恢复 `WS_POPUP` 全屏
- **退出黑屏**：4 级递进（隐藏 → 最小化 → ESC → 杀进程）
- **状态显示**：未运行/运行中(PID+版本)/挂起/无响应

### 3. 驱动卸载（限制解除）

- **解除 U 盘限制**：卸载 `TDFileFilter` 驱动（`sc stop` + `sc delete`，通过 SCM API）
- **解除网络限制**：卸载 `TDNetFilter` 驱动
- 需要管理员权限（manifest 已申请 `requireAdministrator`）

### 4. 学生机房管理助手控制

- **杀进程**：覆盖 v6.8~v12.99 已知进程名（GATESRV/MasterHelper/StudentSrv 等）
- **动态密码计算器**：覆盖 v9.x~v12.0 的 4 套算法

| 版本范围 | 算法 |
|---------|------|
| 10.0 前 | `8` + `16 × (年×91 + 月×13 + 日×57)` |
| 10.0 ~ 11.0 | 上面结果 `+11` |
| 11.0 ~ 11.06 首发版 | `年×789 + 月×123 + 日×456 + 111` |
| 11.06 第三版 ~ 12.0 | `(月×159 + 日×357 + 计算机名末位ASCII×258)` 转 7 进制 |

- **一键解禁系统程序**：CMD / 注册表编辑器 / 任务管理器 / IE 下载 / 运行框 等（清理相关注册表策略键值）
- **重启资源管理器**：杀掉并重启 `explorer.exe`

### 通用功能

- **圆形悬浮窗**：始终置顶，左键弹主菜单（支持拖拽），中键一键广播窗口化，右键快捷菜单
- **托盘图标**：实时显示可见/隐蔽窗口数和列表，右键集成所有功能
- **标题栏右键**：在任意窗口标题栏点右键即可隐蔽/恢复该窗口
- **全局快捷键**：见下表
- **截图预览**：模拟教师端视角，用 `PrintWindow(PW_RENDERFULLCONTENT)` 按 Z 序合成缩略图
- **崩溃诊断**：寄存器 + 栈回溯写入崩溃日志
- **日志系统**：`%TEMP%\MythwareHacker\` 下 `run.log` + `crash.log`
- **状态持久化**：崩溃/退出后重启自动恢复窗口位置

### 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Shift+H` | 隐蔽/恢复当前窗口 |
| `Ctrl+Shift+S` | 鼠标选择模式（十字光标点选窗口） |
| `Ctrl+Shift+P` | 打开/关闭截图预览 |
| `Ctrl+Shift+F` | 显示/隐藏悬浮窗 |
| `Ctrl+Shift+K` | 强杀极域进程 |

---

## 系统要求

- **操作系统**：Windows 7 / 8 / 8.1 / 10 / 11
  - Win10 2004 (Build 19041)+：WDA 全功能
  - Win7/8/旧 Win10：自动降级到屏幕外方案
- **架构**：32 位 (x86) 和 64 位 (x64) 均支持
  - 注入时严格位数匹配：32 位 EXE 只能注入 32 位目标，64 位同理
- **权限**：管理员权限（卸载驱动、操作极域进程需要）
- **依赖**：无运行时依赖，静态链接

---

## 编译方法

### 依赖

- **编译器**：MinGW-w64 (g++) 7.0+，需支持 C++17
- 下载：[MinGW-w64](https://www.mingw-w64.org/) 或 [MSYS2](https://www.msys2.org/)

### Windows 编译

```bat
:: 使用构建脚本（推荐）
scripts\build.bat              :: 构建当前架构
scripts\build.bat 32           :: 强制 32 位
scripts\build.bat 64           :: 强制 64 位
scripts\build.bat all          :: 同时构建 32 和 64 位
scripts\build.bat clean        :: 清理
```

或使用 Makefile（需要 make）：

```bash
make                # 构建当前架构
make ARCH=32        # 强制 32 位
make ARCH=64        # 强制 64 位
make all            # 同时构建 32 和 64 位
make clean          # 清理
```

### 产物

```
bin/
├── MythwareHacker_x86.exe       # 32 位主程序
├── MythwareHacker_x64.exe       # 64 位主程序
├── MythwareHideHook_x86.dll         # 32 位注入 DLL
└── MythwareHideHook_x64.dll         # 64 位注入 DLL
```

**部署时**：EXE 和对应位数的 DLL 必须放在同一目录。程序会根据自身位数加载 `MythwareHideHook_x86.dll` 或 `MythwareHideHook_x64.dll`。

### 手动编译（单文件 DLL）

```bash
g++ -DBUILD_DLL -shared -o MythwareHideHook_x64.dll src/dll/hide_hook.cpp -O2 -s -Iinclude -m64 -static-libstdc++ -static-libgcc -luser32
```

---

## 使用教程

1. **下载/编译**得到 `MythwareHacker_x64.exe`（或 x86）和对应 DLL
2. **双击运行**（会弹出 UAC 提权请求，因为需要管理员权限）
3. 程序启动后**最小化到托盘**（盾牌图标），显示欢迎信息
4. 主要操作：
   - **在任意窗口标题栏上点右键** → 选择"隐蔽此窗口"
   - **右键托盘图标** → 集成菜单（所有功能）
   - **使用快捷键** → 快速操作

### 验证隐蔽效果

按 `Ctrl+Shift+P` 打开截图预览窗口，模拟教师端视角：
- 被隐蔽的窗口在预览中应该显示为**黑块**（WDA 生效）或**不可见**（屏幕外方案）
- 底部红字提示：建议用 `Win+Shift+S` 系统截图工具准确验证

---

## 项目结构

```
MythwareHacker/
├── src/
│   ├── main.cpp              # 主程序入口、消息循环、WndProc、鼠标钩子
│   ├── ui/
│   │   ├── app_state.h       # 全局 UI 上下文
│   │   ├── tray.cpp          # 系统托盘图标
│   │   ├── menu.cpp          # 托盘右键菜单 + 标题栏菜单 + 密码计算器
│   │   ├── float_window.cpp  # 圆形悬浮窗（左/中/右键三功能）
│   │   ├── hotkey.cpp        # 全局快捷键注册
│   │   └── preview.cpp       # 截图预览窗口（模拟教师端视角）
│   ├── core/
│   │   ├── window_hide.cpp   # 窗口隐蔽（四套方案）
│   │   ├── process_control.cpp # 极域进程控制（杀/挂起/恢复/广播/黑屏）
│   │   ├── driver_control.cpp  # 驱动卸载（TDFileFilter/TDNetFilter）
│   │   ├── mythware_control.cpp # 学生机房管理助手控制
│   │   ├── password_calc.cpp # 动态密码计算器（4 套算法）
│   │   └── inject.cpp        # 通用 DLL 注入（CreateRemoteThread）
│   ├── utils/
│   │   ├── log.cpp           # 日志系统 + 崩溃诊断
│   │   ├── window_utils.cpp  # 窗口枚举/标题/进程名工具
│   │   └── persist.cpp       # 状态持久化（崩溃恢复）
│   └── dll/
│       └── hide_hook.cpp     # 注入用 DLL（RemoteSetAffinity 导出函数）
├── include/
│   ├── common.h              # 公共头（常量、版本检测、WDA 宏）
│   ├── ui/                   # UI 模块头文件
│   ├── core/                 # 核心模块头文件
│   └── utils/                # 工具模块头文件
├── res/
│   ├── app.manifest          # UAC 提权 + 可视化样式 + DPI 感知
│   └── app.rc                # 版本信息资源
├── scripts/
│   └── build.bat             # Windows 构建脚本（32/64 双架构）
├── Makefile                  # GNU Make 构建脚本
└── README.md
```

---

## 技术原理

### 窗口隐蔽 - WDA_EXCLUDEFROMCAPTURE

Windows 10 2004 引入 `WDA_EXCLUDEFROMCAPTURE`，调用 `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` 后，该窗口在所有截图/录屏 API 中显示为黑块，但对用户正常可见。

**跨进程问题**：`SetWindowDisplayAffinity` 只能对本进程拥有的窗口生效。要隐蔽其他进程的窗口，必须通过 DLL 注入，在目标进程上下文中调用。

**注入流程**（`src/core/inject.cpp`）：
1. `OpenProcess` 获取目标进程句柄
2. 检查位数匹配（`IsWow64Process`）
3. `VirtualAllocEx` + `WriteProcessMemory` 写入 DLL 路径
4. `CreateRemoteThread` 调用 `LoadLibraryW` 加载 DLL 到目标进程
5. 本地 `LoadLibrary` 同一 DLL，`GetProcAddress` 拿导出函数地址
6. `CreateToolhelp32Snapshot` 找目标进程中同名模块基址
7. `远程基址 + (本地函数地址 - 本地基址)` 计算远程函数地址
8. 再次 `CreateRemoteThread` 调用远程函数，传入参数

### 驱动卸载

极域通过内核驱动实现限制：
- `TDFileFilter.sys`：文件系统过滤驱动，拦截 U 盘文件访问
- `TDNetFilter.sys`：网络过滤驱动，拦截网络访问

通过 SCM API（`OpenSCManager` → `OpenService` → `ControlService(STOP)` → `DeleteService`）停止并删除驱动服务。

### 动态密码算法

学生机房管理助手使用基于日期的动态密码，不同版本算法不同。详见 [MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) README 中的算法说明。本项目的 `src/core/password_calc.cpp` 实现了 4 套算法的自动选择。

---

## 致谢

本项目整合了以下三个开源项目的核心功能：

- **[JiYuTrainer](https://github.com/imengyu/JiYuTrainer)** by [imengyu](https://github.com/imengyu) - 极域对抗工具（MIT）
- **[MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit)** by [BengbuGuards](https://github.com/BengbuGuards) - 极域工具包
- **MythwareHide** - 窗口隐蔽工具（基于 SetWindowDisplayAffinity）

感谢原作者的开源贡献。

---

## 许可

MIT License
