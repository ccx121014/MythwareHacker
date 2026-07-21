// process_control.cpp - 极域进程控制实现
#include "core/process_control.h"
#include "utils/log.h"

namespace pctl {

static const wchar_t* STUDENT_MAIN = L"StudentMain.exe";

static bool g_mythwareSuspended = false;

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

MythwareStatus GetMythwareStatus()
{
    MythwareStatus status = {};
    status.state = MythwareState::NotRunning;
    status.pid = FindProcessByName(STUDENT_MAIN);

    if (status.pid == 0) {
        g_mythwareSuspended = false;
        return status;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, status.pid);
    if (!hProcess) {
        status.state = MythwareState::NoResponse;
        return status;
    }

    // 获取版本信息
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameExW(hProcess, nullptr, exePath, MAX_PATH) > 0) {
        status.exePath = exePath;
        // 读取版本资源
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
                    status.version = buf;
                }
            }
        }
    }

    // 简化：判断是否无响应
    if (g_mythwareSuspended) {
        status.state = MythwareState::Suspended;
    } else if (WaitForInputIdle(hProcess, 1000) == WAIT_FAILED) {
        status.state = MythwareState::NoResponse;
    } else {
        status.state = MythwareState::Running;
    }

    CloseHandle(hProcess);
    return status;
}

bool KillMythware()
{
    DWORD pid = FindProcessByName(STUDENT_MAIN);
    if (pid == 0) {
        logger::Info(L"极域未运行，无需杀进程");
        return true;
    }

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

// 内部：查找极域广播窗口
// 极域广播窗口类名通常是 "T.TopDomain.Student.BroadcastForm" 或类似
// 同时查找多种可能的窗口类名
struct FindBroadcastData { HWND hwnd; };

static BOOL CALLBACK FindBroadcastProc(HWND hwnd, LPARAM lParam)
{
    auto* d = reinterpret_cast<FindBroadcastData*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t cls[256] = {};
    wchar_t title[256] = {};
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, title, 256);

    // 极域广播窗口类名特征：
    // - T.TopDomain.Student.BroadcastForm
    // - TopDomain
    // - Broadcast
    // 或者标题包含"广播"
    bool isBroadcast = wcsstr(cls, L"TopDomain") ||
                        wcsstr(cls, L"Broadcast") ||
                        wcsstr(cls, L"Mythware") ||
                        wcsstr(title, L"广播") ||
                        wcsstr(title, L"Broad");

    if (!isBroadcast) return TRUE;

    // 检查是否接近全屏（广播窗口通常是全屏或接近全屏）
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    // 窗口至少占屏幕 70% 才认为是广播窗口
    if (winW >= screenW * 0.7 && winH >= screenH * 0.7) {
        d->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND FindBroadcastWindow()
{
    FindBroadcastData data = {};
    EnumWindows(FindBroadcastProc, reinterpret_cast<LPARAM>(&data));
    return data.hwnd;
}

bool BroadcastToWindowed()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到极域广播窗口");
        return false;
    }

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    logger::Info(L"找到广播窗口: 类名=" + std::wstring(cls));

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // 方案1：先尝试发送 WM_COMMAND 模拟点击"窗口化"按钮
    // 极域广播窗口的窗口化按钮命令 ID 可能为 1004 或其他
    // 尝试多个可能的命令 ID
    const int cmdIds[] = {1004, 1005, 1006, 1003, 1002, 1001};
    for (int id : cmdIds) {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        Sleep(200);

        // 检查窗口是否已经变小
        RECT rc;
        GetWindowRect(hwnd, &rc);
        if (rc.right - rc.left < screenW * 0.9 || rc.bottom - rc.top < screenH * 0.9) {
            logger::Info(L"已通过 WM_COMMAND(ID=" + WSTR(id) + L") 将广播窗口化");
            return true;
        }
    }

    // 方案2：直接修改窗口样式
    // 先获取当前样式
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    // 移除 WS_POPUP（全屏弹窗样式），添加 WS_OVERLAPPEDWINDOW（标准窗口样式）
    style &= ~WS_POPUP;
    style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    SetWindowLong(hwnd, GWL_STYLE, style);

    // 取消 WS_EX_TOPMOST 如果有
    exStyle &= ~WS_EX_TOPMOST;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    // 设置窗口位置和大小（屏幕中央，50% 大小）
    int w = screenW * 0.6;
    int h = screenH * 0.7;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;
    SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    // 强制刷新
    SetForegroundWindow(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);

    logger::Info(L"已通过修改样式将广播窗口化");
    return true;
}

bool BroadcastToFullscreen()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style |= WS_POPUP;
    style &= ~WS_OVERLAPPEDWINDOW;
    SetWindowLong(hwnd, GWL_STYLE, style);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, screenW, screenH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    logger::Info(L"已将广播全屏化");
    return true;
}

bool SetBroadcastTopmost(bool enable)
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) {
        logger::Warn(L"未找到广播窗口，无法设置置顶");
        return false;
    }

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (enable) {
        exStyle |= WS_EX_TOPMOST;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        logger::Info(L"已将广播窗口设为置顶");
    } else {
        exStyle &= ~WS_EX_TOPMOST;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        logger::Info(L"已取消广播窗口置顶");
    }
    return true;
}

bool IsBroadcastTopmost()
{
    HWND hwnd = FindBroadcastWindow();
    if (!hwnd) return false;
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    return (exStyle & WS_EX_TOPMOST) != 0;
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

} // namespace pctl
