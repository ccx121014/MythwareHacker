# MythwareHacker

> 集大成应用：整合 [JiYuTrainer](https://github.com/imengyu/JiYuTrainer) + [MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) + MythwareHide 三项目核心功能
>
> 极域电子教室 + 学生机房管理助手 综合对抗工具包

**当前版本：v1.2.0**

---

## 目录

- [功能](#功能)
- [快捷键](#快捷键)
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
- **广播窗口化**：查找极域全屏广播窗口（PID+类名前缀+标题匹配），双重方案：
  - 方案1（推荐）：`PostMessage(WM_COMMAND, MAKEWPARAM(1004, BM_CLICK))` 模拟点击极域内部按钮
  - 方案2（兜底）：直接修改窗口样式 `WS_BORDER | WS_OVERLAPPEDWINDOW`，自动调整子窗口 `"TDDesk Render Window"` 大小
- **广播全屏化**：恢复 `WS_POPUP` 全屏，发送 `WM_SIZE` 通知窗口重排
- **广播置顶开关**：一键切换广播窗口置顶状态，持续维持线程确保生效（对抗极域反复重置）
- **退出黑屏**：4 级递进（隐藏 → 最小化 → ESC → 杀进程）
- **状态显示**：未运行/运行中(PID+版本)/挂起/无响应
- **自动监控**：后台线程每 3 秒检测极域状态，黑屏时自动窗口化广播
- **进程防杀保护**：守护线程 + SE_DEBUG 权限，进程被杀自动重启
- **开机自启动**：注册表 Run 键，随系统启动

### 3. 驱动卸载（限制解除）

- **解除 U 盘限制**：卸载 `TDFileFilter` 驱动（`sc stop` + `sc delete`，通过 SCM API）
- **解除网络限制**：卸载 `TDNetFilter` 驱动
- **解除键盘锁**：极域禁用键盘后恢复（三重防御：低级键盘钩子 + 远程进程内 `BlockInput(FALSE)` + `RegisterHotKey` 备份）
- **一键解除全部**：一次性解除所有限制
- 需要管理员权限（manifest 已申请 `requireAdministrator`）

### 4. 学生机房管理助手控制

- **杀进程**：覆盖 v6.8~v12.99 已知进程名（GATESRV/MasterHelper/StudentSrv 等），先停止 `zmserv` 服务再杀进程，防止服务监控自动重启
- **动态密码计算器**：覆盖 v9.x~v12.0 的 4 套算法

| 版本范围 | 算法 |
|---------|------|
| 10.0 前 | `8` + `16 × (年×91 + 月×13 + 日×57)` |
| 10.0 ~ 11.0 | 上面结果 `+11` |
| 11.0 ~ 11.06 首发版 | `年×789 + 月×123 + 日×456 + 111` |
| 11.06 第三版 ~ 12.0 | `(月×159 + 日×357 + 计算机名末位ASCII×258)` 转 7 进制 |

- **一键解禁系统程序**：CMD / 注册表编辑器 / 任务管理器 / IE 下载 / 运行框 等（清理相关注册表策略键值）
- **重启资源管理器**：杀掉并重启 `explorer.exe`

### 5. 防截屏保护

主窗口和悬浮窗均启用 `WDA_EXCLUDEFROMCAPTURE`，极域截屏/录屏时显示为黑块，防止操作被教师端捕捉。

### 6. 置顶策略

- 主窗口和悬浮窗始终置顶于极域窗口之上（参考 MythwareToolkit 策略）
- **不降级极域窗口**：双方都是 `WS_EX_TOPMOST`，靠 Z 序竞争而非反复修改样式，避免卡顿
- 轮询间隔优化为 1 秒，减少系统消耗

### 通用功能

- **圆形悬浮窗**：始终置顶，左键弹主菜单（支持拖拽），中键一键广播窗口化，右键快捷菜单
- **托盘图标**：实时显示可见/隐蔽窗口数和列表，右键集成所有功能
- **标题栏右键**：在任意窗口标题栏点右键即可隐蔽/恢复该窗口
- **全局快捷键**：12 个快捷键，支持低级键盘钩子（极域禁用键盘时仍可使用），详见 [快捷键](#快捷键)
- **截图预览**：模拟教师端视角，从屏幕 DC 直接复制（性能优化，避免逐窗口 `PrintWindow` 卡顿）
- **崩溃诊断**：寄存器 + 栈回溯写入崩溃日志
- **日志系统**：`%TEMP%\MythwareHacker\` 下 `run.log` + `crash.log`
- **状态持久化**：崩溃/退出后重启自动恢复窗口位置

---

## 快捷键

> 采用**低级键盘钩子 (WH_KEYBOARD_LL)**，极域广播禁用键盘时仍可使用！

| 快捷键 | 功能 |
|--------|------|
| `Alt+B` | 唤起主窗口 |
| `Ctrl+Shift+H` | 隐蔽/恢复当前窗口 |
| `Ctrl+Shift+S` | 鼠标选择模式（十字光标点选窗口） |
| `Ctrl+Shift+P` | 打开/关闭截图预览 |
| `Ctrl+Shift+F` | 显示/隐藏悬浮窗 |
| `Ctrl+Shift+K` | 强杀极域进程 |
| `Ctrl+Shift+W` | 广播窗口化 |
| `Ctrl+Shift+X` | 退出黑屏 |
| `Ctrl+Shift+M` | 挂起/恢复极域（切换） |
| `Ctrl+Shift+C` | 杀掉机房助手 |
| `Ctrl+Shift+U` | 一键解除全部 |
| `Ctrl+Shift+R` | 重启资源管理器 |

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
├── MythwareHideHook_x64.dll         # 64 位注入 DLL
└── inject_helper_x86.exe            # 32 位注入助手（64位程序注入32位目标用）
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
   - **使用快捷键** → 快速操作（极域禁用键盘时仍可用）
   - **按 Alt+B** → 随时唤起主窗口

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
│   │   ├── main_window.cpp   # 主窗口
│   │   ├── tray.cpp          # 系统托盘图标
│   │   ├── menu.cpp          # 托盘右键菜单 + 标题栏菜单
│   │   ├── float_window.cpp  # 圆形悬浮窗（左/中/右键三功能）
│   │   ├── hotkey.cpp        # 全局快捷键 + 低级键盘钩子
│   │   └── preview.cpp       # 截图预览窗口（模拟教师端视角）
│   ├── core/
│   │   ├── window_hide.cpp   # 窗口隐蔽（四套方案）
│   │   ├── process_control.cpp # 极域进程控制（杀/挂起/恢复/广播/黑屏/自动监控）
│   │   ├── self_protect.cpp  # 防杀进程保护（守护线程 + SE_DEBUG）
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

### 低级键盘钩子绕过极域键盘禁用

极域广播时会禁用键盘（通常通过键盘钩子或 `BlockInput`），导致 `RegisterHotKey` 注册的快捷键失效。

**三重防御策略**：

1. **低级键盘钩子（WH_KEYBOARD_LL）**：在系统键盘输入路径的更底层工作，在应用层钩子之前执行，可以绕过极域的键盘禁用。检测到快捷键后通过 `PostMessage` 投递给主窗口消息循环处理。

2. **远程进程内解除 BlockInput**：通过 `CreateRemoteThread` 在极域进程内调用 `BlockInput(FALSE)`，从源头解除键盘锁定。

3. **RegisterHotKey 备份**：同时注册传统热键作为备份方案。

**性能优化**：钩子竞争线程间隔从 300ms 优化为 3 秒，BlockInput 绕过线程间隔从 200ms 优化为 5 秒，大幅降低系统开销。

### 置顶策略

极域会不断设置自己为 TOPMOST，导致窗口层级互相抢占。

**优化策略**（参考 MythwareToolkit）：

1. **不再降级极域窗口**：双方都是 `WS_EX_TOPMOST`，靠 Z 序竞争而非反复修改样式，避免系统卡顿
2. **只确保自身置顶**：轮询线程每 1 秒调用 `SetWindowPos(hwnd, HWND_TOPMOST, ...)`，让自己保持在最前面
3. **广播窗口置顶维持**：专门的维持线程每 1 秒检查一次，对抗极域反复重置广播窗口置顶状态

### 防杀进程保护

1. **守护线程**：监控父进程句柄，进程终止时自动重启
2. **SE_DEBUG 权限**：提升进程权限，增强保护能力
3. **开机自启动**：注册表 Run 键，随系统启动

### 广播窗口化技术原理

**窗口查找算法**（参考 JiYuTrainer + MythwareToolkit）：

1. **PID 过滤**：通过 `CreateToolhelp32Snapshot` 查找 `StudentMain.exe` 进程
2. **类名前缀匹配**：类名前 4 字符为 `"Afx:"`（MFC 框架特征）
3. **标题匹配**：标题包含 `"广播"`、`"演示"`、`"共享"` 或等于 `"屏幕广播"`、`"屏幕演播室窗口"`
4. **缓存优化**：缓存 PID 和 HWND，避免每次都枚举进程和窗口

**窗口化/全屏化切换**（双重方案）：

1. **方案1（MythwareToolkit 方法）**：向广播窗口发送 `PostMessage(WM_COMMAND, MAKEWPARAM(1004, BM_CLICK))`，模拟点击极域内部的窗口化/全屏化切换按钮。这是最可靠的方式，让极域自身完成状态转换。

2. **方案2（JiYuTrainer 方法）**：直接修改窗口样式作为兜底方案：
   - 窗口化：添加 `WS_BORDER | WS_OVERLAPPEDWINDOW`，移除 `WS_EX_TOPMOST`，调整子窗口 `"TDDesk Render Window"` 大小
   - 全屏化：移除 `WS_BORDER | WS_OVERLAPPEDWINDOW`，添加 `WS_EX_TOPMOST`，`SetWindowPos` 拉满屏幕，发送 `WM_SIZE` 通知重排

### 性能优化

针对之前版本存在的卡顿问题，进行了全面优化：

| 优化项 | 优化前 | 优化后 | 效果 |
|--------|--------|--------|------|
| 钩子竞争线程 | 300ms 轮询 | 3 秒间隔 | 系统调用减少 10 倍 |
| BlockInput 绕过线程 | 200ms 轮询 | 5 秒间隔 | 远程线程创建减少 25 倍 |
| 广播置顶线程 | 500ms 轮询 + 每次枚举 | 1 秒间隔 + 缓存 | 避免重复枚举窗口 |
| 悬浮窗置顶线程 | 500ms | 1 秒 | 减少系统消耗 |
| 主窗口置顶线程 | 500ms | 1 秒 | 减少系统消耗 |
| 监控线程 | 2 秒 | 3 秒 | 参考 JiYuTrainer 策略 |
| GetMythwareStatus | 每次查询版本号 | 缓存版本号 | 避免重复文件 IO |
| 截图预览 | 逐窗口 PrintWindow | 屏幕 DC 直接复制 | 性能提升数十倍 |

### 驱动卸载

极域通过内核驱动实现限制：
- `TDFileFilter.sys`：文件系统过滤驱动，拦截 U 盘文件访问
- `TDNetFilter.sys`：网络过滤驱动，拦截网络访问

通过 SCM API（`OpenSCManager` → `OpenService` → `ControlService(STOP)` → `DeleteService`）停止并删除驱动服务。

### 动态密码算法

学生机房管理助手使用基于日期的动态密码，不同版本算法不同。详见 [MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) README 中的算法说明。本项目的 `src/core/password_calc.cpp` 实现了 4 套算法的自动选择。

### 崩溃诊断

使用 `SetUnhandledExceptionFilter` 注册全局异常处理，捕获程序崩溃时：
1. `SymInitialize` 初始化符号解析
2. `StackWalk64` 生成栈回溯
3. 收集系统信息、异常码、寄存器状态
4. 写入 `%TEMP%\MythwareHacker\crash.log`

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
