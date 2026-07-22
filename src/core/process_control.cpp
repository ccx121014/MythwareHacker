// process_control.cpp - 极域进程控制实现
// 广播窗口化/全屏化/置顶算法完全参考 JiYuTrainer
#include "core/process_control.h"
#include "utils/log.h"

namespace pctl {

static const wchar_t* STUDENT_MAIN = L"StudentMain.exe";

static bool g_mythwareSuspended = false;

// === 广播置顶开关（采用 JiYuTrainer 思路：全局变量 + 持续维持线程）===
static bool g_broadcastTopmostEnabled = false;
static bool g_setAllowGbTop = false;   // 模仿 JiYuTrainer: 允许广播窗口置顶
static HANDLE g_hBroadcastTopmostThread = nullptr;
static bool g_broadcastTopmostRunning = false;
static HANDLE g_hBroadcastFixThread = nullptr;
static bool g_broadcastFixRunning = false;

static DWORD g_cachedBroadcastPid = 0;

static DWORD FindProcessByName(const std::wstring& name);
static void EnsureBroadcastFixThread();
static void StopBroadcastFixThread();

// 内部：通过进程名查找 PID
static DWORD FindProcessByName(const std::wstring& name)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

// 内部：从注册表读取极域安装路径
static std::wstring GetMythwarePath()
{
    HKEY hKey = nullptr;
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);

    // 常见注册表位置
    const wchar_t* keys[] = {
        L"SOFTWARE\\TopDomain\\e-Learning Class Standard\\1.00",
        L"SOFTWARE\\TopDomain\\e-learning Class Standard",
        L"SOFTWARE\\TopDomain\\极域电子教室",
        nullptr
    };

    for (int i = 0; keys[i]; i++) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keys[i], 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
            // 尝试 "TargetDirectory"（原版 MythwareToolkit 使用的值名）
            // 和 "Path"（常见值名）
            bool found = false;
            for (const wchar_t* valName : { L"TargetDirectory", L"Path" }) {
                size = sizeof(path);
                if (RegQueryValueExW(hKey, valName, nullptr, nullptr, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                    found = true;
                    break;
                }
            }
            RegCloseKey(hKey);
            if (found) {
                std::wstring p(path);
                if (!p.empty() && p.back() != L'\\') p += L'\\';
                p += STUDENT_MAIN;
                return p;
            }
        }
    }
    return L"";
}

static std::wstring g_cachedVersion;
static DWORD g_cachedPid = 0;

MythwareStatus GetMythwareStatus()
{
    MythwareStatus status = {};
    status.state = MythwareState::NotRunning;
    status.pid = FindProcessByName(STUDENT_MAIN);

    if (status.pid == 0) {
        g_mythwareSuspended = false;
        g_cachedPid = 0;
        g_cachedVersion.clear();
        return status;
    }

    // 如果 PID 变了或版本缓存为空，重新查询版本号（重量级操作，只做一次）
    if (status.pid != g_cachedPid || g_cachedVersion.empty()) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, status.pid);
        if (hProcess) {
            wchar_t exePath[MAX_PATH] = {};
            if (GetModuleFileNameExW(hProcess, nullptr, exePath, MAX_PATH) > 0) {
                status.exePath = exePath;
                DWORD dummy = 0;
                DWORD verSize = GetFileVersionInfoSizeW(exePath, &dummy);
                if (verSize > 0) {
                    std::vector<BYTE> verData(verSize);
                    if (GetFileVersionInfoW(exePath, 0, verSize, verData.data())) {
                        VS_FIXEDFILEINFO* ffi = nullptr;
                        UINT ffiLen = 0;
                        if (VerQueryValueW(verData.data(), L"\\", (LPVOID*)&ffi, &ffiLen) && ffi) {
                            wchar_t buf[64];
                            swprintf(buf, 64, L"%d.%d.%d.%d",
                                     HIWORD(ffi->dwProductVersionMS), LOWORD(ffi->dwProductVersionMS),
                                     HIWORD(ffi->dwProductVersionLS), LOWORD(ffi->dwProductVersionLS));
                            g_cachedVersion = buf;
                        }
                    }
                }
            }
            CloseHandle(hProcess);
        }
        g_cachedPid = status.pid;
    }
    status.version = g_cachedVersion;

    if (g_mythwareSuspended) {
        status.state = MythwareState::Suspended;
    } else {
        status.state = MythwareState::Running;
    }

    return status;
}

bool KillMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) {
        logger::Info(L"极域未运行，无需杀进程");
        return true;
    }

    // 清除缓存
    g_cachedPid = 0;
    g_cachedVersion.clear();
    g_cachedBroadcastPid = 0;

    // 方法1：直接 TerminateProcess（需要 PROCESS_TERMINATE 权限）
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (hProcess) {
        if (TerminateProcess(hProcess, 1)) {
            WaitForSingleObject(hProcess, 3000);
            CloseHandle(hProcess);
            logger::Info(L"已杀掉极域进程 PID=" + WSTR(pid));
            return true;
        }
        CloseHandle(hProcess);
    }

    // 方法2：用 DebugActiveProcessStop（先附加再停止，可绕过部分保护）
    if (DebugActiveProcess(pid)) {
        Sleep(200);
        DebugActiveProcessStop(pid);
        logger::Info(L"通过 Debug 方式停止极域 PID=" + WSTR(pid));
        return true;
    }

    logger::Error(L"杀极域进程失败 PID=" + WSTR(pid) + L" 错误: " + WSTR(GetLastError()));
    return false;
}

bool StartMythware()
{
    std::wstring path = GetMythwarePath();
    if (path.empty()) {
        logger::Error(L"找不到极域安装路径");
        return false;
    }

    // 降权到登录用户启动（避免以管理员权限启动极域）
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // 用 explorer.exe 作为父进程启动（实现降权）
    // 简化实现：直接 CreateProcess
    std::wstring cmd = L"\"" + path + L"\"";

    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        logger::Info(L"已启动极域: " + path);
        return true;
    }

    logger::Error(L"启动极域失败: " + path + L" 错误: " + WSTR(GetLastError()));
    return false;
}

// 内部：枚举进程所有线程，挂起或恢复
static bool ToggleProcessThreads(DWORD pid, bool suspend)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    int count = 0;

    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread) {
                    if (suspend)
                        SuspendThread(hThread);
                    else
                        ResumeThread(hThread);
                    CloseHandle(hThread);
                    count++;
                }
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);

    logger::Info(std::wstring(suspend ? L"挂起" : L"恢复") + L"极域 " +
              WSTR(count) + L" 个线程");
    return count > 0;
}

bool SuspendMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;
    if (ToggleProcessThreads(pid, true)) {
        g_mythwareSuspended = true;
        return true;
    }
    return false;
}

bool ResumeMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;
    if (ToggleProcessThreads(pid, false)) {
        g_mythwareSuspended = false;
        return true;
    }
    return false;
}

// 内部：判断窗口标题是否是广播窗口（参考 JiYuTrainer::CheckWindowTextIsGb）
static bool IsBroadcastTitle(const wchar_t* text)
{
    if (!text || !*text) return false;
    return (wcsstr(text, L"广播") != nullptr) ||
           (wcsstr(text, L"演示") != nullptr) ||
           (wcsstr(text, L"共享") != nullptr) ||
           (wcscmp(text, L"屏幕演播室窗口") == 0);
}

// 内部：判断窗口是否属于极域进程
static bool IsMythwareWindow(HWND hwnd, DWORD mythwarePid)
{
    if (mythwarePid == 0) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == mythwarePid;
}

struct FindBroadcastData { DWORD pid; HWND hwnd; };

// 枚举回调：找到极域广播窗口
static BOOL CALLBACK FindBroadcastProc(HWND hwnd, LPARAM lParam)
{
    auto* d = reinterpret_cast<FindBroadcastData*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!IsMythwareWindow(hwnd, d->pid)) return TRUE;

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    if (!IsBroadcastTitle(title)) return TRUE;

    d->hwnd = hwnd;
    return FALSE;
}

// 查找极域广播窗口（参考 JiYuTrainer）
static HWND FindBroadcastWindow()
{
    DWORD pid = g_cachedBroadcastPid;
    if (pid == 0) {
        pid = FindProcessByName(STUDENT_MAIN);
        if (pid != 0) g_cachedBroadcastPid = pid;
    } else {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            g_cachedBroadcastPid = 0;
            pid = FindProcessByName(STUDENT_MAIN);
            if (pid != 0) g_cachedBroadcastPid = pid;
        } else {
            CloseHandle(h);
        }
    }

    if (pid == 0) return nullptr;

    FindBroadcastData data = { pid, nullptr };
    EnumWindows(FindBroadcastProc, reinterpret_cast<LPARAM>(&data));
    return data.hwnd;
}

// 检查广播窗口是否已窗口化（参考 JiYuTrainer）
static bool IsBroadcastWindowed(HWND hwnd)
{
    if (!hwnd) return false;
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    // 已窗口化：包含 BORDER 或 OVERLAPPED 标志
    return (style & (WS_BORDER | WS_OVERLAPPEDWINDOW)) != 0;
}

// 调整广播窗口内部的渲染子窗口 "TDDesk Render Window"（参考 JiYuTrainer）
static void ResizeBroadcastChild(HWND hwndParent, int w, int h)
{
    HWND hChild = FindWindowExW(hwndParent, nullptr, nullptr, L"TDDesk Render Window");
    if (hChild) {
        MoveWindow(hChild, 0, 0, w, h, TRUE);
    }
}

// 广播窗口化（参考 JiYuTrainer FakeFull(false)）
bool BroadcastToWindowed()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到极域广播窗口");
        return false;
    }

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    logger::Info(L"窗口化广播窗口: " + std::wstring(title));

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // JiYuTrainer 风格：直接修改窗口样式 + XOR 反转 BORDER/OVERLAPPEDWINDOW
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    // XOR 反转 BORDER 和 OVERLAPPEDWINDOW 标志（核心技巧）
    style ^= (WS_BORDER | WS_OVERLAPPEDWINDOW);
    SetWindowLong(hwnd, GWL_STYLE, style);

    // 默认情况下广播窗口化时应该不置顶（与 g_setAllowGbTop 状态一致）
    if (!g_setAllowGbTop) {
        exStyle &= ~WS_EX_TOPMOST;
    } else {
        exStyle |= WS_EX_TOPMOST;
    }
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    // 重新计算窗口位置和大小（3/4 宽 × 4/5 高，居中）
    int w = (int)((double)screenW * (3.0 / 4.0));
    int h = (int)((double)screenH * (4.0 / 5.0));
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    SetWindowPos(hwnd, g_setAllowGbTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_DRAWFRAME);

    // 调整子窗口 + 通知窗口重排
    SendMessageW(hwnd, WM_SIZE, 0, MAKEWPARAM(w, h));
    ResizeBroadcastChild(hwnd, w, h);

    logger::Info(L"广播窗口已窗口化");
    return true;
}

// 广播全屏化（参考 JiYuTrainer FakeFull(true) / ManualFull(true)）
bool BroadcastToFullscreen()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到极域广播窗口");
        return false;
    }

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    logger::Info(L"全屏化广播窗口: " + std::wstring(title));

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // JiYuTrainer 风格：直接修改窗口样式
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    // XOR 反转 BORDER 和 OVERLAPPEDWINDOW 标志
    style ^= (WS_BORDER | WS_OVERLAPPEDWINDOW);
    SetWindowLong(hwnd, GWL_STYLE, style);

    // 全屏时强制置顶
    exStyle |= WS_EX_TOPMOST;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, screenW, screenH,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_DRAWFRAME);

    // 通知窗口重排 + 调整子窗口
    SendMessageW(hwnd, WM_SIZE, 0, MAKEWPARAM(screenW, screenH));
    ResizeBroadcastChild(hwnd, screenW, screenH);

    logger::Info(L"广播窗口已全屏化");
    return true;
}

// 广播窗口置顶开关（参考 JiYuTrainer::ManualTop）
bool SetBroadcastTopmost(bool enable)
{
    // 模仿 JiYuTrainer 思路：修改全局状态，线程持续维持
    g_broadcastTopmostEnabled = enable;
    g_setAllowGbTop = enable;

    // 立即尝试设置当前窗口
    HWND hwnd = FindBroadcastWindow();
    if (hwnd) {
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        if (enable) {
            exStyle |= WS_EX_TOPMOST;
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            exStyle &= ~WS_EX_TOPMOST;
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        logger::Info(enable ? L"广播窗口置顶已开启" : L"广播窗口置顶已关闭");
    } else {
        logger::Info(enable ? L"广播窗口置顶已开启（等待广播窗口出现）"
                            : L"广播窗口置顶已关闭（等待广播窗口出现）");
    }

    // 启动持续维持线程（JiYuTrainer 风格）
    EnsureBroadcastFixThread();
    return true;
}

bool IsBroadcastTopmost()
{
    return g_broadcastTopmostEnabled;
}

// === JiYuTrainer 风格：持续监控广播窗口的线程 ===
// 定期检查广播窗口，根据全局状态修复其置顶/窗口化
// 注意：只有用户显式设置过置顶开关后才执行修改，避免默认情况下与极域争抢
static DWORD WINAPI BroadcastFixThreadProc(LPVOID)
{
    while (g_broadcastFixRunning) {
        // 只有用户显式设置过置顶开关，才执行修改
        // 默认状态下不干涉极域窗口
        if (!g_broadcastTopmostEnabled && !g_setAllowGbTop) {
            Sleep(500);
            continue;
        }

        HWND hwnd = FindBroadcastWindow();
        if (hwnd) {
            LONG oldLong = GetWindowLong(hwnd, GWL_EXSTYLE);
            if (!g_setAllowGbTop && (oldLong & WS_EX_TOPMOST)) {
                SetWindowLong(hwnd, GWL_EXSTYLE, oldLong ^ WS_EX_TOPMOST);
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else if (g_setAllowGbTop && !(oldLong & WS_EX_TOPMOST)) {
                SetWindowLong(hwnd, GWL_EXSTYLE, oldLong | WS_EX_TOPMOST);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }

        Sleep(500);
    }
    return 0;
}

static void EnsureBroadcastFixThread()
{
    if (g_hBroadcastFixThread) return;
    g_broadcastFixRunning = true;
    g_hBroadcastFixThread = CreateThread(nullptr, 0, BroadcastFixThreadProc, nullptr, 0, nullptr);
}

static void StopBroadcastFixThread()
{
    g_broadcastFixRunning = false;
    if (g_hBroadcastFixThread) {
        WaitForSingleObject(g_hBroadcastFixThread, 1000);
        CloseHandle(g_hBroadcastFixThread);
        g_hBroadcastFixThread = nullptr;
    }
}

struct BlackScreenData { HWND hwnd; };

static BOOL CALLBACK FindBlackScreenProc(HWND hwnd, LPARAM lParam)
{
    auto* d = reinterpret_cast<BlackScreenData*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t cls[256] = {};
    wchar_t title[256] = {};
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, title, 256);

    RECT rc;
    GetWindowRect(hwnd, &rc);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    if ((wcsstr(cls, L"TopDomain") || wcsstr(cls, L"BlackScreen") ||
         wcsstr(title, L"黑屏") || wcsstr(title, L"安静") || wcsstr(title, L"Black")) &&
        rc.right - rc.left >= screenW * 0.95 &&
        rc.bottom - rc.top >= screenH * 0.95) {
        d->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool ExitBlackScreen()
{
    BlackScreenData data = {};
    EnumWindows(FindBlackScreenProc, reinterpret_cast<LPARAM>(&data));

    if (!data.hwnd) {
        logger::Info(L"未找到黑屏窗口");
        return false;
    }

    HWND hwnd = data.hwnd;

    ShowWindow(hwnd, SW_HIDE);
    if (!IsWindowVisible(hwnd)) {
        logger::Info(L"黑屏窗口已隐藏");
        return true;
    }

    ShowWindow(hwnd, SW_MINIMIZE);
    Sleep(100);
    if (!IsWindowVisible(hwnd)) return true;

    PostMessage(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
    PostMessage(hwnd, WM_KEYUP, VK_ESCAPE, 0);
    Sleep(200);
    if (!IsWindowVisible(hwnd)) return true;

    PostMessage(hwnd, WM_CLOSE, 0, 0);
    Sleep(200);
    if (!IsWindowVisible(hwnd)) {
        logger::Info(L"黑屏窗口已通过 WM_CLOSE 关闭");
        return true;
    }

    logger::Warn(L"所有级别均未能退出黑屏窗口");
    return false;
}

bool IsBlackScreenActive()
{
    BlackScreenData data = {};
    EnumWindows(FindBlackScreenProc, reinterpret_cast<LPARAM>(&data));
    return data.hwnd != nullptr;
}

struct DemoteData { DWORD pid; };

static BOOL CALLBACK DemoteMythwareProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    auto* data = reinterpret_cast<DemoteData*>(lParam);

    if (windowPid != data->pid) return TRUE;

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOPMOST) {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TOPMOST);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return TRUE;
}

void DemoteMythwareWindows()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return;

    DemoteData data = { pid };
    EnumWindows(DemoteMythwareProc, reinterpret_cast<LPARAM>(&data));
}

bool UnblockInputInMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, pid);
    if (!hProcess) {
        logger::Warn(L"UnblockInput: OpenProcess 失败 PID=" + WSTR(pid) + L" err=" + WSTR(GetLastError()));
        return false;
    }

    // 获取本地 BlockInput 地址和 user32 基址
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    FARPROC pBlockInput = GetProcAddress(hUser32, "BlockInput");
    if (!pBlockInput) {
        CloseHandle(hProcess);
        return false;
    }

    // 计算远程进程中 BlockInput 的地址
    // user32.dll 是系统 DLL，通常在所有进程中基址相同
    // 为保险，通过 EnumProcessModules 获取远程基址
    FARPROC remoteBlockInput = pBlockInput;  // 默认假设基址相同
    HMODULE hMods[1024] = {};
    DWORD cbNeeded = 0;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModName[MAX_PATH] = {};
            if (GetModuleFileNameExW(hProcess, hMods[i], szModName, MAX_PATH)) {
                if (wcsstr(szModName, L"user32.dll") != nullptr) {
                    remoteBlockInput = reinterpret_cast<FARPROC>(
                        reinterpret_cast<UINT_PTR>(hMods[i]) +
                        (reinterpret_cast<UINT_PTR>(pBlockInput) - reinterpret_cast<UINT_PTR>(hUser32)));
                    break;
                }
            }
        }
    }

    // 在极域进程内创建远程线程调用 BlockInput(FALSE)
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBlockInput),
        reinterpret_cast<LPVOID>(FALSE), 0, nullptr);
    if (!hThread) {
        logger::Warn(L"UnblockInput: CreateRemoteThread 失败 err=" + WSTR(GetLastError()));
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    logger::Info(L"已在极域进程内解除 BlockInput");
    return true;
}

void CleanupBroadcastTopmost()
{
    g_broadcastTopmostRunning = false;
    if (g_hBroadcastTopmostThread) {
        DWORD result = WaitForSingleObject(g_hBroadcastTopmostThread, 500);
        if (result != WAIT_OBJECT_0) {
            TerminateThread(g_hBroadcastTopmostThread, 0);
        }
        CloseHandle(g_hBroadcastTopmostThread);
        g_hBroadcastTopmostThread = nullptr;
    }
    StopBroadcastFixThread();
}

} // namespace pctl
