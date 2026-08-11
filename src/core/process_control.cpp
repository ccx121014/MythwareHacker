// process_control.cpp - 极域进程控制（JiYuTrainer架构改进版）
// ============================================================
// 广播处理全新架构（完全对齐JiYuTrainer的DLL注入+消息通信模式）：
//   1. 监控线程每2500ms扫描一次极域进程
//   2. 发现 StudentMain.exe 后自动注入 MythwareHideHook.dll
//   3. DLL 在极域内创建消息窗口 "MythwareHacker MsgCenter Window"
//   4. 主程序通过 WM_COPYDATA 发送命令：fakeFull / manualFull / manualTop / unlockInput
//   5. DLL 对广播窗口子类化，拦截 WM_WINDOWPOSCHANGING —— 从**源头**消除置顶死循环
//      （之前是外部改完极域又改回去，现在极域改的时候直接被钩子拦截了）
// ============================================================
#include "core/process_control.h"
#include "core/inject.h"
#include "utils/log.h"

namespace pctl {

static const wchar_t* STUDENT_MAIN = L"StudentMain.exe";
static const wchar_t* HOOK_DLL_NAME = L"MythwareHideHook.dll";
static const wchar_t* MSG_WINDOW_TITLE = L"MythwareHacker MsgCenter Window";

// ==================== 全局状态 ====================
static bool g_mythwareSuspended = false;

static std::wstring g_cachedVersion;
static DWORD g_cachedPid = 0;
static DWORD g_lastInjectedPid = 0;  // 上次注入成功的PID，用于检测重启

// 用户意图开关（BroadcastToWindowed/Fullscreen/Topmost 会改这些）
static bool g_userWantTopmost = false;
static bool g_userWantFullscreen = false;  // true=全屏, false=窗口化

// 监控线程
static HANDLE g_hMonitorThread = nullptr;
static volatile bool g_monitorRunning = false;

static DWORD WINAPI MonitorThreadProc(LPVOID);
static void EnsureMonitorThread();
static void StopMonitorThread();

static DWORD FindProcessByName(const std::wstring& name);
static std::wstring GetMythwarePath();

// 内部：枚举极域所有广播/全屏窗口并发送给 DLL 子类化
static void EnumAndSendBroadcastHwnds(DWORD mythwarePid);

// 内部：向 DLL 发送 WM_COPYDATA 命令（FindWindow 消息窗口）
// 如果发送失败（DLL未就绪）返回false
static bool SendToDll(const std::wstring& cmd);

// 内部：旧版外部修改（fallback，DLL不可用时用）
static bool LegacyBroadcastWindowed();
static bool LegacyBroadcastFullscreen();
static bool LegacySetTopmost(bool enable);

// ==================== 进程查找/基础控制 ====================

static DWORD FindProcessByName(const std::wstring& name)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe); DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                pid = pe.th32ProcessID; break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap); return pid;
}

static std::wstring GetMythwarePath()
{
    HKEY hKey = nullptr;
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    const wchar_t* keys[] = {
        L"SOFTWARE\\TopDomain\\e-Learning Class Standard\\1.00",
        L"SOFTWARE\\TopDomain\\e-learning Class Standard",
        L"SOFTWARE\\TopDomain\\极域电子教室",
        nullptr
    };
    for (int i = 0; keys[i]; i++) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keys[i], 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
            bool found = false;
            for (const wchar_t* vn : { L"TargetDirectory", L"Path" }) {
                size = sizeof(path);
                if (RegQueryValueExW(hKey, vn, nullptr, nullptr, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                    found = true; break;
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
    if (status.pid != g_cachedPid || g_cachedVersion.empty()) {
        HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, status.pid);
        if (hp) {
            wchar_t exePath[MAX_PATH] = {};
            if (GetModuleFileNameExW(hp, nullptr, exePath, MAX_PATH) > 0) {
                status.exePath = exePath;
                DWORD dummy = 0;
                DWORD vs = GetFileVersionInfoSizeW(exePath, &dummy);
                if (vs > 0) {
                    std::vector<BYTE> vd(vs);
                    if (GetFileVersionInfoW(exePath, 0, vs, vd.data())) {
                        VS_FIXEDFILEINFO* ffi = nullptr; UINT ffiLen = 0;
                        if (VerQueryValueW(vd.data(), L"\\", (LPVOID*)&ffi, &ffiLen) && ffi) {
                            wchar_t buf[64];
                            swprintf(buf, 64, L"%d.%d.%d.%d",
                                     HIWORD(ffi->dwProductVersionMS), LOWORD(ffi->dwProductVersionMS),
                                     HIWORD(ffi->dwProductVersionLS), LOWORD(ffi->dwProductVersionLS));
                            g_cachedVersion = buf;
                        }
                    }
                }
            }
            CloseHandle(hp);
        }
        g_cachedPid = status.pid;
    }
    status.version = g_cachedVersion;
    status.state = g_mythwareSuspended ? MythwareState::Suspended : MythwareState::Running;
    // 确保监控线程在运行
    EnsureMonitorThread();
    return status;
}

bool KillMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) {
        logger::Info(L"极域未运行，无需杀进程");
        return true;
    }
    // 优先：发送 boom 命令让极域自己退出（干净无残留）
    SendToDll(L"boom");
    Sleep(200);

    g_cachedPid = 0; g_cachedVersion.clear(); g_lastInjectedPid = 0;

    HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (hp) {
        if (TerminateProcess(hp, 1)) {
            WaitForSingleObject(hp, 3000);
            CloseHandle(hp);
            logger::Info(L"已杀掉极域进程 PID=" + WSTR(pid));
            return true;
        }
        CloseHandle(hp);
    }
    if (DebugActiveProcess(pid)) {
        Sleep(200); DebugActiveProcessStop(pid);
        logger::Info(L"通过 Debug 方式停止极域 PID=" + WSTR(pid));
        return true;
    }
    logger::Error(L"杀极域进程失败 PID=" + WSTR(pid) + L" 错误: " + WSTR(GetLastError()));
    return false;
}

bool StartMythware()
{
    std::wstring path = GetMythwarePath();
    if (path.empty()) { logger::Error(L"找不到极域安装路径"); return false; }
    STARTUPINFOW si = {}; si.cb = sizeof(si); PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + path + L"\"";
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                             nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        logger::Info(L"已启动极域: " + path);
        EnsureMonitorThread();
        return true;
    }
    logger::Error(L"启动极域失败: " + path + L" 错误: " + WSTR(GetLastError()));
    return false;
}

static bool ToggleProcessThreads(DWORD pid, bool suspend)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te; te.dwSize = sizeof(te); int count = 0;
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (ht) {
                    suspend ? SuspendThread(ht) : ResumeThread(ht);
                    CloseHandle(ht); count++;
                }
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    logger::Info(std::wstring(suspend ? L"挂起" : L"恢复") + L"极域 " + WSTR(count) + L" 个线程");
    return count > 0;
}

bool SuspendMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;
    if (ToggleProcessThreads(pid, true)) { g_mythwareSuspended = true; return true; }
    return false;
}
bool ResumeMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;
    if (ToggleProcessThreads(pid, false)) { g_mythwareSuspended = false; return true; }
    return false;
}

// ==================== 广播窗口检测（JiYuTrainer同款标题匹配） ====================

static bool IsBroadcastTitle(const wchar_t* text)
{
    if (!text || !*text) return false;
    return (wcsstr(text, L"广播") != nullptr) ||
           (wcsstr(text, L"演示") != nullptr) ||
           (wcsstr(text, L"共享") != nullptr) ||
           (wcscmp(text, L"屏幕演播室窗口") == 0);
}
static bool IsMythwareWindow(HWND hwnd, DWORD mp)
{
    if (mp == 0) return false; DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid); return pid == mp;
}

struct FindGbData { DWORD pid; HWND hwnd; };
static BOOL CALLBACK FindGbProc(HWND hwnd, LPARAM lp)
{
    auto* d = (FindGbData*)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!IsMythwareWindow(hwnd, d->pid)) return TRUE;
    wchar_t t[256] = {}; GetWindowTextW(hwnd, t, 256);
    if (!IsBroadcastTitle(t)) return TRUE;
    d->hwnd = hwnd; return FALSE;
}
static HWND FindBroadcastWindow()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return nullptr;
    FindGbData d = {pid, nullptr};
    EnumWindows(FindGbProc, (LPARAM)&d);
    return d.hwnd;
}

// 枚举极域所有广播/全屏窗口，通过 WM_COPYDATA 逐个发给 DLL
struct EnumAndSendData { DWORD pid; int count; };
static BOOL CALLBACK EnumAndSendProc(HWND hwnd, LPARAM lp)
{
    auto* d = (EnumAndSendData*)lp;
    if (!IsMythwareWindow(hwnd, d->pid)) return TRUE;
    wchar_t t[256] = {}; GetWindowTextW(hwnd, t, 256);
    RECT rc; GetWindowRect(hwnd, &rc);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    bool isGb = IsBroadcastTitle(t) || wcscmp(t, L"BlackScreen Window") == 0;
    bool isFull = (rc.top == 0 && rc.left == 0 && rc.right >= sw && rc.bottom >= sh);
    if (isGb || isFull) {
        wchar_t buf[64];
        swprintf(buf, 64, L"hwf:%lld", (ULONG_PTR)(LONG_PTR)hwnd);
        // 不记录发送是否成功（monitor loop 会继续重试）
        inject::SendCmdToMsgCenter(MSG_WINDOW_TITLE, buf);
        d->count++;
    }
    return TRUE;
}
static void EnumAndSendBroadcastHwnds(DWORD mythwarePid)
{
    EnumAndSendData d = {mythwarePid, 0};
    EnumWindows(EnumAndSendProc, (LPARAM)&d);
}

// ==================== DLL 消息通信 ====================

static bool SendToDll(const std::wstring& cmd)
{
    return inject::SendCmdToMsgCenter(MSG_WINDOW_TITLE, cmd);
}

// ==================== 监控线程（JiYuTrainer RunResetPid + RunCk 合并） ====================

static void EnsureMonitorThread()
{
    if (g_hMonitorThread) return;
    g_monitorRunning = true;
    g_hMonitorThread = CreateThread(nullptr, 0, MonitorThreadProc, nullptr, 0, nullptr);
}
static void StopMonitorThread()
{
    g_monitorRunning = false;
    if (g_hMonitorThread) {
        WaitForSingleObject(g_hMonitorThread, 5000);
        CloseHandle(g_hMonitorThread);
        g_hMonitorThread = nullptr;
    }
}

static DWORD WINAPI MonitorThreadProc(LPVOID)
{
    logger::Info(L"极域注入监控线程已启动（间隔 2500ms）");
    bool injectedOnce = false;
    while (g_monitorRunning) {
        DWORD pid = FindProcessByName(STUDENT_MAIN);

        if (pid == 0) {
            // 极域退出了，清缓存
            g_lastInjectedPid = 0;
            injectedOnce = false;
        } else {
            // ========== (1) 检测PID变化 -> 自动注入DLL ==========
            if (pid != g_lastInjectedPid) {
                // 先尝试查找DLL消息窗口，可能DLL已经由别人注入过
                HWND existing = FindWindowW(nullptr, MSG_WINDOW_TITLE);
                if (existing) {
                    g_lastInjectedPid = pid;
                    logger::Info(L"DLL消息窗口已存在，跳过注入");
                } else {
                    auto res = inject::InjectDllOnly(pid, HOOK_DLL_NAME);
                    if (res.success) {
                        g_lastInjectedPid = pid;
                        injectedOnce = true;
                        logger::Info(L"自动注入DLL成功，PID=" + WSTR(pid));
                        // 等待DLL创建消息窗口（最多等待1秒）
                        for (int i = 0; i < 10; i++) {
                            if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) break;
                            Sleep(100);
                        }
                    } else {
                        logger::Warn(L"自动注入DLL失败：" + res.error);
                        // 3秒后再试
                        Sleep(3000);
                        if (!g_monitorRunning) break;
                        continue;
                    }
                }
            }

            // ========== (2) 枚举窗口 + 发送给 DLL 子类化 ==========
            // 只要消息窗口存在，就发送窗口句柄（频率：每次循环都发）
            if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
                EnumAndSendBroadcastHwnds(pid);

                // ========== (3) 持续强制同步用户意图 ==========
                // 广播窗口每次出现极域都会尝试置顶+全屏，必须持续覆盖。
                // 尤其：用户没开置顶时，必须每次都发 manualTop:false，
                // 否则极域的定时器会把广播窗口重新设为 TOPMOST，覆盖掉我们的悬浮窗/主窗口。
                if (g_userWantTopmost) {
                    SendToDll(L"manualTop:true");
                } else {
                    SendToDll(L"manualTop:false");
                }
                if (g_userWantFullscreen) {
                    SendToDll(L"manualFull:true");
                } else {
                    SendToDll(L"fakeFull:false");
                    SendToDll(L"manualFull:false");
                }
            }
        }

        // 2500ms —— 和JiYuTrainer默认3100ms同一量级
        Sleep(2500);
    }
    logger::Info(L"极域注入监控线程已停止");
    return 0;
}

// ==================== 广播控制：优先 DLL 命令，失败走 Legacy ====================

bool BroadcastToWindowed()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到极域广播窗口（广播未启动？）");
        // 记住用户意图，等窗口出现时监控线程会同步
        g_userWantFullscreen = false;
        // 尝试先发命令，DLL后续注入后会由监控线程同步意图
        SendToDll(L"manualFull:false");
        SendToDll(L"fakeFull:false");
        return true;  // 意图已记录，不算失败
    }

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    logger::Info(L"窗口化广播窗口: " + std::wstring(title));

    g_userWantFullscreen = false;

    // === 方式 1：发命令给 DLL（WNDPROC子类化 + 全局fakeFull开关，拦截SetWindowPos死循环）===
    bool dllOk = false;
    if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
        bool a = SendToDll(L"manualFull:false");
        bool b = SendToDll(L"fakeFull:false");
        dllOk = a && b;
    }

    // === 方式 2（fallback）：外部修改样式 ===
    bool legOk = false;
    if (!dllOk) {
        legOk = LegacyBroadcastWindowed();
    }

    if (dllOk || legOk) {
        logger::Info(dllOk ? L"广播窗口化(DLL注入模式)成功"
                           : L"广播窗口化(Legacy外部模式)成功");
        return true;
    }
    return false;
}

bool BroadcastToFullscreen()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到极域广播窗口（广播未启动？）");
        g_userWantFullscreen = true;
        SendToDll(L"manualFull:true");
        SendToDll(L"fakeFull:true");
        return true;
    }
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    logger::Info(L"全屏化广播窗口: " + std::wstring(title));

    g_userWantFullscreen = true;

    bool dllOk = false;
    if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
        dllOk = SendToDll(L"manualFull:true");
    }
    bool legOk = false;
    if (!dllOk) legOk = LegacyBroadcastFullscreen();

    if (dllOk || legOk) {
        logger::Info(dllOk ? L"广播全屏化(DLL注入模式)成功"
                           : L"广播全屏化(Legacy外部模式)成功");
        return true;
    }
    return false;
}

bool SetBroadcastTopmost(bool enable)
{
    g_userWantTopmost = enable;

    HWND hwnd = FindBroadcastWindow();
    if (hwnd) {
        bool dllOk = false;
        if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
            dllOk = SendToDll(enable ? L"manualTop:true" : L"manualTop:false");
        }
        bool legOk = false;
        if (!dllOk) legOk = LegacySetTopmost(enable);

        if (dllOk || legOk) {
            logger::Info(enable ? (dllOk ? L"广播置顶(DLL)已开启" : L"广播置顶(Legacy)已开启")
                               : (dllOk ? L"广播置顶(DLL)已关闭" : L"广播置顶(Legacy)已关闭"));
        }
    } else {
        // 没找到窗口：仍然发送命令（监控线程发现新窗口时会同步意图到DLL）
        if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
            SendToDll(enable ? L"manualTop:true" : L"manualTop:false");
        }
        logger::Info(enable ? L"广播置顶意图已记录（等待广播窗口出现）"
                            : L"广播置顶意图已清除（等待广播窗口出现）");
    }
    return true;
}

bool IsBroadcastTopmost()
{
    return g_userWantTopmost;
}

// ==================== 检测/切换 全屏↔窗口化 ====================

bool IsBroadcastFullscreen()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    // 无边框(无WS_BORDER/WS_OVERLAPPEDWINDOW / 有WS_POPUP) 且尺寸接近全屏 → 视为全屏
    bool noBorder = !(style & WS_BORDER) && !(style & WS_OVERLAPPEDWINDOW);
    bool sizeOk = (rc.left <= 0 && rc.top <= 0 && rc.right >= sw - 2 && rc.bottom >= sh - 2);
    return noBorder && sizeOk;
}

bool ToggleBroadcastMode()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        // 无窗口 → 记录意图为窗口化；等窗口出现后监控线程会应用
        logger::Warn(L"未找到广播窗口，切换默认执行→窗口化");
        return BroadcastToWindowed();
    }
    if (IsBroadcastFullscreen()) {
        logger::Info(L"切换广播：全屏 → 窗口化");
        return BroadcastToWindowed();
    } else {
        logger::Info(L"切换广播：窗口化 → 全屏");
        return BroadcastToFullscreen();
    }
}

// ============================================================
// Legacy Fallback 实现（DLL不可用时仍能工作）
// ============================================================
static void ResizeGbChild(HWND p, int w, int h)
{
    HWND c = FindWindowExW(p, nullptr, nullptr, L"TDDesk Render Window");
    if (c) MoveWindow(c, 0, 0, w, h, TRUE);
}
static bool LegacyBroadcastWindowed()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style |= (WS_BORDER | WS_OVERLAPPEDWINDOW);
    style &= ~WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    int w = (int)((double)sw * 3.0 / 4.0);
    int h = (int)((double)sh * 4.0 / 5.0);
    int x = (sw - w) / 2, y = (sh - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_DRAWFRAME | SWP_NOZORDER);
    SendMessageW(hwnd, WM_SIZE, 0, MAKEWPARAM(w, h));
    ResizeGbChild(hwnd, w, h);
    return true;
}
static bool LegacyBroadcastFullscreen()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_BORDER | WS_OVERLAPPEDWINDOW);
    style |= WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, sw, sh,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_DRAWFRAME | SWP_NOZORDER);
    SendMessageW(hwnd, WM_SIZE, 0, MAKEWPARAM(sw, sh));
    ResizeGbChild(hwnd, sw, sh);
    return true;
}
static bool LegacySetTopmost(bool enable)
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (enable) {
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return true;
}

// ==================== 黑屏 / 降级 / UnblockInput ====================

struct BsData { HWND hwnd; };
static BOOL CALLBACK FindBsProc(HWND hwnd, LPARAM lp)
{
    auto* d = (BsData*)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[256]={}, t[256]={};
    GetClassNameW(hwnd, cls, 256); GetWindowTextW(hwnd, t, 256);
    RECT rc; GetWindowRect(hwnd, &rc);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if ((wcsstr(cls, L"TopDomain") || wcsstr(cls, L"BlackScreen") ||
         wcsstr(t, L"黑屏") || wcsstr(t, L"安静") || wcsstr(t, L"Black")) &&
        (rc.right-rc.left) >= sw*0.95 && (rc.bottom-rc.top) >= sh*0.95) {
        d->hwnd = hwnd; return FALSE;
    }
    return TRUE;
}
bool ExitBlackScreen()
{
    BsData d = {}; EnumWindows(FindBsProc, (LPARAM)&d);
    if (!d.hwnd) { logger::Info(L"未找到黑屏窗口"); return false; }
    HWND h = d.hwnd;
    ShowWindow(h, SW_HIDE);
    if (!IsWindowVisible(h)) { logger::Info(L"黑屏窗口已隐藏"); return true; }
    ShowWindow(h, SW_MINIMIZE); Sleep(100);
    if (!IsWindowVisible(h)) return true;
    PostMessage(h, WM_KEYDOWN, VK_ESCAPE, 0);
    PostMessage(h, WM_KEYUP, VK_ESCAPE, 0); Sleep(200);
    if (!IsWindowVisible(h)) return true;
    PostMessage(h, WM_CLOSE, 0, 0); Sleep(200);
    if (!IsWindowVisible(h)) { logger::Info(L"黑屏窗口已通过 WM_CLOSE 关闭"); return true; }
    logger::Warn(L"所有级别均未能退出黑屏窗口");
    return false;
}
bool IsBlackScreenActive()
{
    BsData d = {}; EnumWindows(FindBsProc, (LPARAM)&d); return d.hwnd != nullptr;
}

struct DmData { DWORD pid; };
static BOOL CALLBACK DemoteProc(HWND hwnd, LPARAM lp)
{
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD wp; GetWindowThreadProcessId(hwnd, &wp);
    auto* d = (DmData*)lp;
    if (wp != d->pid) return TRUE;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOPMOST) {
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return TRUE;
}
void DemoteMythwareWindows()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return;
    DmData d = {pid}; EnumWindows(DemoteProc, (LPARAM)&d);
}

bool UnblockInputInMythware()
{
    // 优先：调用 DLL 中的 unlockInput 命令（直接在极域进程内调 UnLockLocalInput / DoneHook）
    bool dllOk = false;
    if (FindWindowW(nullptr, MSG_WINDOW_TITLE)) {
        dllOk = SendToDll(L"unlockInput");
    }
    if (dllOk) {
        logger::Info(L"已通过DLL解除极域BlockInput");
        return true;
    }
    // fallback：远程线程调 BlockInput(FALSE)
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) return false;
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                            PROCESS_VM_OPERATION, FALSE, pid);
    if (!hp) { logger::Warn(L"UnblockInput: OpenProcess 失败 PID=" + WSTR(pid)); return false; }
    HMODULE hu = GetModuleHandleA("user32.dll");
    FARPROC pBI = GetProcAddress(hu, "BlockInput");
    if (!pBI) { CloseHandle(hp); return false; }
    FARPROC rBI = pBI;
    HMODULE mods[1024] = {}; DWORD cb = 0;
    if (EnumProcessModules(hp, mods, sizeof(mods), &cb)) {
        for (DWORD i = 0; i < (cb / sizeof(HMODULE)); i++) {
            wchar_t n[MAX_PATH] = {};
            if (GetModuleFileNameExW(hp, mods[i], n, MAX_PATH) &&
                wcsstr(n, L"user32.dll") != nullptr) {
                rBI = (FARPROC)((UINT_PTR)mods[i] +
                       ((UINT_PTR)pBI - (UINT_PTR)hu));
                break;
            }
        }
    }
    HANDLE ht = CreateRemoteThread(hp, nullptr, 0,
              (LPTHREAD_START_ROUTINE)rBI, (LPVOID)FALSE, 0, nullptr);
    if (!ht) { CloseHandle(hp); return false; }
    WaitForSingleObject(ht, 1000); CloseHandle(ht); CloseHandle(hp);
    logger::Info(L"已在极域进程内调用 BlockInput(FALSE)");
    return true;
}

void CleanupBroadcastTopmost()
{
    StopMonitorThread();
}

} // namespace pctl
