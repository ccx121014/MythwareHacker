// hotkey.cpp - 全局快捷键实现（含低级键盘钩子，绕过极域键盘禁用）
#include "ui/hotkey.h"
#include "ui/main_window.h"
#include "ui/float_window.h"
#include "ui/preview.h"
#include "ui/app_state.h"
#include "core/process_control.h"
#include "core/mythware_control.h"
#include "core/driver_control.h"
#include "core/window_hide.h"
#include "utils/log.h"

namespace hotkey {

static HHOOK g_hLowLevelHook = nullptr;
static HWND  g_hMainWnd = nullptr;
static bool  g_ctrlDown = false;
static bool  g_shiftDown = false;
static bool  g_altDown = false;
static bool  g_hookRunning = false;
static HANDLE g_hHookThread = nullptr;
static HANDLE g_hBlockInputThread = nullptr;
static DWORD  g_lastHookInstallTick = 0;
static DWORD  g_lastBlockInputTick = 0;
static bool   g_lastHotkeyBlocked = false;  // 上次快捷键是否被极域拦截

// Ctrl+Shift 快捷键映射表
static struct { UINT vk; int id; const wchar_t* name; } g_ctrlShiftKeys[] = {
    { 'H', ID_HOTKEY_HIDE_CURRENT,     L"Ctrl+Shift+H" },
    { 'S', ID_HOTKEY_SELECT,           L"Ctrl+Shift+S" },
    { 'P', ID_HOTKEY_PREVIEW,          L"Ctrl+Shift+P" },
    { 'F', ID_HOTKEY_FLOAT,            L"Ctrl+Shift+F" },
    { 'K', ID_HOTKEY_KILL_MYTHWARE,    L"Ctrl+Shift+K" },
    { 'W', ID_HOTKEY_BROADCAST_WIN,    L"Ctrl+Shift+W" },
    { 'G', ID_HOTKEY_BROADCAST_FULLSCREEN, L"Ctrl+Shift+G" },
    { 'X', ID_HOTKEY_EXIT_BLACK,       L"Ctrl+Shift+X" },
    { 'M', ID_HOTKEY_SUSPEND_RESUME,   L"Ctrl+Shift+M" },
    { 'C', ID_HOTKEY_KILL_CLASSROOM,   L"Ctrl+Shift+C" },
    { 'U', ID_HOTKEY_UNBLOCK_ALL,      L"Ctrl+Shift+U" },
    { 'R', ID_HOTKEY_RESTART_EXPLORER, L"Ctrl+Shift+R" },
};

// Alt 快捷键
static struct { UINT vk; int id; const wchar_t* name; } g_altKeys[] = {
    { 'B', ID_HOTKEY_SHOW_MAIN, L"Alt+B" },
};

static void ExecuteHotkey(int id)
{
    if (g_hMainWnd) {
        PostMessageW(g_hMainWnd, WM_HOTKEY, (WPARAM)id, 0);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp   = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        if (isDown || isUp) {
            bool pressed = isDown;
            switch (kbd->vkCode) {
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
                g_ctrlDown = pressed;
                break;
            case VK_SHIFT:
            case VK_LSHIFT:
            case VK_RSHIFT:
                g_shiftDown = pressed;
                break;
            case VK_MENU:
            case VK_LMENU:
            case VK_RMENU:
                g_altDown = pressed;
                break;
            default: {
                if (isDown && !(kbd->flags & LLKHF_UP)) {
                    // Ctrl+Shift 组合
                    if (g_ctrlDown && g_shiftDown) {
                        for (auto& k : g_ctrlShiftKeys) {
                            if (kbd->vkCode == k.vk) {
                                ExecuteHotkey(k.id);
                                // 返回 1 阻止消息继续传递，
                                // 这样极域的钩子收不到这个按键
                                return 1;
                            }
                        }
                    }
                    // Alt 组合
                    if (g_altDown && !g_ctrlDown) {
                        for (auto& k : g_altKeys) {
                            if (kbd->vkCode == k.vk) {
                                ExecuteHotkey(k.id);
                                return 1;
                            }
                        }
                    }
                }
                break;
            }
            }
        }
    }
    return CallNextHookEx(g_hLowLevelHook, nCode, wParam, lParam);
}

static void DoInstallHook()
{
    if (g_hLowLevelHook) {
        UnhookWindowsHookEx(g_hLowLevelHook);
        g_hLowLevelHook = nullptr;
    }
    g_hLowLevelHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                         GetModuleHandleW(nullptr), 0);
    g_lastHookInstallTick = GetTickCount();
}

// 钩子维持线程（参考 MythwareToolkit：低频重装，非高频竞争）
// 策略：只在检测到极域可能覆盖了我们的钩子时才重装，大幅减少系统开销
static DWORD WINAPI HookCompeteThreadProc(LPVOID)
{
    while (g_hookRunning) {
        // 基础间隔 3 秒（MythwareToolkit 便携版 250ms 是因为它没有其他线程，
        // 我们有多个后台线程，需要更长的间隔避免系统卡顿）
        Sleep(3000);

        // 只在极域运行时才重装钩子，避免空转
        // 简化检测：如果距上次安装超过 3 秒就重装一次
        DWORD now = GetTickCount();
        if (now - g_lastHookInstallTick > 3000) {
            DoInstallHook();
        }
    }
    return 0;
}

// BlockInput 绕过线程（极低频，只在极域运行且键盘被锁时才执行）
static DWORD WINAPI BlockInputBypassThreadProc(LPVOID)
{
    while (g_hookRunning) {
        // 5 秒间隔，远低于原来的 200-500ms
        // BlockInput 只在极域主动发起键盘锁定时需要解除
        // 极域不会每秒都调 BlockInput，5 秒检查一次足够
        Sleep(5000);

        DWORD now = GetTickCount();
        if (now - g_lastBlockInputTick < 5000) continue;

        // 只在极域运行时才尝试解除
        // 用简单 PID 检测代替完整的 GetMythwareStatus（避免快照+版本查询开销）
        DWORD pid = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"StudentMain.exe") == 0) {
                        pid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }

        if (pid != 0) {
            pctl::UnblockInputInMythware();
            BlockInput(FALSE);
            g_lastBlockInputTick = GetTickCount();
        }
    }
    return 0;
}

bool InstallLowLevelHook()
{
    if (g_hLowLevelHook) return true;
    DoInstallHook();
    if (!g_hLowLevelHook) {
        logger::Warn(L"低级键盘钩子安装失败: " + WSTR(GetLastError()));
        return false;
    }
    logger::Info(L"低级键盘钩子已安装（绕过极域键盘禁用）");

    // 启动钩子竞争线程
    if (!g_hookRunning) {
        g_hookRunning = true;
        g_hHookThread = CreateThread(nullptr, 0, HookCompeteThreadProc, nullptr, 0, nullptr);
        g_hBlockInputThread = CreateThread(nullptr, 0, BlockInputBypassThreadProc, nullptr, 0, nullptr);
    }
    return true;
}

void UninstallLowLevelHook()
{
    g_hookRunning = false;

    if (g_hHookThread) {
        WaitForSingleObject(g_hHookThread, 500);
        TerminateThread(g_hHookThread, 0);
        CloseHandle(g_hHookThread);
        g_hHookThread = nullptr;
    }
    if (g_hBlockInputThread) {
        WaitForSingleObject(g_hBlockInputThread, 500);
        TerminateThread(g_hBlockInputThread, 0);
        CloseHandle(g_hBlockInputThread);
        g_hBlockInputThread = nullptr;
    }

    if (g_hLowLevelHook) {
        UnhookWindowsHookEx(g_hLowLevelHook);
        g_hLowLevelHook = nullptr;
        logger::Info(L"低级键盘钩子已卸载");
    }
}

void RegisterAll(HWND hWnd)
{
    g_hMainWnd = hWnd;
    // 优先安装低级键盘钩子（绕过极域键盘禁用）
    InstallLowLevelHook();
    // 同时注册系统热键作为备份
    struct { int id; UINT mod; UINT vk; const wchar_t* name; } keys[] = {
        { ID_HOTKEY_SHOW_MAIN,        MOD_ALT,                'B', L"Alt+B" },
        { ID_HOTKEY_HIDE_CURRENT,     MOD_CONTROL | MOD_SHIFT, 'H', L"Ctrl+Shift+H" },
        { ID_HOTKEY_SELECT,           MOD_CONTROL | MOD_SHIFT, 'S', L"Ctrl+Shift+S" },
        { ID_HOTKEY_PREVIEW,          MOD_CONTROL | MOD_SHIFT, 'P', L"Ctrl+Shift+P" },
        { ID_HOTKEY_FLOAT,            MOD_CONTROL | MOD_SHIFT, 'F', L"Ctrl+Shift+F" },
        { ID_HOTKEY_KILL_MYTHWARE,    MOD_CONTROL | MOD_SHIFT, 'K', L"Ctrl+Shift+K" },
        { ID_HOTKEY_BROADCAST_WIN,    MOD_CONTROL | MOD_SHIFT, 'W', L"Ctrl+Shift+W" },
        { ID_HOTKEY_BROADCAST_FULLSCREEN, MOD_CONTROL | MOD_SHIFT, 'G', L"Ctrl+Shift+G" },
        { ID_HOTKEY_EXIT_BLACK,       MOD_CONTROL | MOD_SHIFT, 'X', L"Ctrl+Shift+X" },
        { ID_HOTKEY_SUSPEND_RESUME,   MOD_CONTROL | MOD_SHIFT, 'M', L"Ctrl+Shift+M" },
        { ID_HOTKEY_KILL_CLASSROOM,   MOD_CONTROL | MOD_SHIFT, 'C', L"Ctrl+Shift+C" },
        { ID_HOTKEY_UNBLOCK_ALL,      MOD_CONTROL | MOD_SHIFT, 'U', L"Ctrl+Shift+U" },
        { ID_HOTKEY_RESTART_EXPLORER, MOD_CONTROL | MOD_SHIFT, 'R', L"Ctrl+Shift+R" },
    };
    int okCount = 0;
    for (auto& k : keys) {
        if (RegisterHotKey(hWnd, k.id, k.mod, k.vk)) okCount++;
    }
    logger::Info(L"系统热键注册: " + WSTR(okCount) + L"/" + WSTR(_countof(keys)));
}

void UnregisterAll(HWND hWnd)
{
    UninstallLowLevelHook();
    UnregisterHotKey(hWnd, ID_HOTKEY_SHOW_MAIN);
    UnregisterHotKey(hWnd, ID_HOTKEY_HIDE_CURRENT);
    UnregisterHotKey(hWnd, ID_HOTKEY_SELECT);
    UnregisterHotKey(hWnd, ID_HOTKEY_PREVIEW);
    UnregisterHotKey(hWnd, ID_HOTKEY_FLOAT);
    UnregisterHotKey(hWnd, ID_HOTKEY_KILL_MYTHWARE);
    UnregisterHotKey(hWnd, ID_HOTKEY_BROADCAST_WIN);
    UnregisterHotKey(hWnd, ID_HOTKEY_BROADCAST_FULLSCREEN);
    UnregisterHotKey(hWnd, ID_HOTKEY_EXIT_BLACK);
    UnregisterHotKey(hWnd, ID_HOTKEY_SUSPEND_RESUME);
    UnregisterHotKey(hWnd, ID_HOTKEY_KILL_CLASSROOM);
    UnregisterHotKey(hWnd, ID_HOTKEY_UNBLOCK_ALL);
    UnregisterHotKey(hWnd, ID_HOTKEY_RESTART_EXPLORER);
}

bool Handle(WPARAM wParam)
{
    switch (wParam) {
    case ID_HOTKEY_SHOW_MAIN:
        mainwin::Show();
        return true;
    case ID_HOTKEY_HIDE_CURRENT:
        whide::ToggleCurrent();
        return true;
    case ID_HOTKEY_SELECT:
        app::g_ctx.selectMode = true;
        SetCursor(app::g_ctx.hCursorCross);
        ShowWindow(app::g_ctx.hWndMain, SW_HIDE);
        logger::Info(L"已进入选择模式");
        return true;
    case ID_HOTKEY_PREVIEW:
        preview::Toggle();
        return true;
    case ID_HOTKEY_FLOAT:
        floatw::Toggle();
        return true;
    case ID_HOTKEY_KILL_MYTHWARE:
        pctl::KillMythware();
        logger::Info(L"已强杀极域进程");
        return true;
    case ID_HOTKEY_BROADCAST_WIN:
        pctl::BroadcastToWindowed();
        logger::Info(L"已将广播窗口化");
        return true;
    case ID_HOTKEY_BROADCAST_FULLSCREEN:
        pctl::BroadcastToFullscreen();
        logger::Info(L"已将广播全屏化");
        return true;
    case ID_HOTKEY_EXIT_BLACK:
        pctl::ExitBlackScreen();
        logger::Info(L"已尝试退出黑屏");
        return true;
    case ID_HOTKEY_SUSPEND_RESUME: {
        auto status = pctl::GetMythwareStatus();
        if (status.state == pctl::MythwareState::Suspended) {
            pctl::ResumeMythware();
            logger::Info(L"已恢复极域进程");
        } else {
            pctl::SuspendMythware();
            logger::Info(L"已挂起极域进程");
        }
        return true;
    }
    case ID_HOTKEY_KILL_CLASSROOM: {
        auto r = mctl::KillClassroomHelper();
        logger::Info(L"杀机房助手: " + WSTR(r.killedCount) + L" 个进程");
        return true;
    }
    case ID_HOTKEY_UNBLOCK_ALL: {
        auto r = drvctl::UnblockAll();
        logger::Info(L"一键解除全部: " + r.detail);
        return true;
    }
    case ID_HOTKEY_RESTART_EXPLORER:
        mctl::RestartExplorer();
        logger::Info(L"已重启资源管理器");
        return true;
    }
    return false;
}

} // namespace hotkey
