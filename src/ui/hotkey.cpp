// hotkey.cpp - 全局快捷键实现
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

void RegisterAll(HWND hWnd)
{
    struct { int id; UINT mod; UINT vk; const wchar_t* name; } keys[] = {
        { ID_HOTKEY_SHOW_MAIN,        MOD_ALT,                'B', L"Alt+B" },
        { ID_HOTKEY_HIDE_CURRENT,     MOD_CONTROL | MOD_SHIFT, 'H', L"Ctrl+Shift+H" },
        { ID_HOTKEY_SELECT,           MOD_CONTROL | MOD_SHIFT, 'S', L"Ctrl+Shift+S" },
        { ID_HOTKEY_PREVIEW,          MOD_CONTROL | MOD_SHIFT, 'P', L"Ctrl+Shift+P" },
        { ID_HOTKEY_FLOAT,            MOD_CONTROL | MOD_SHIFT, 'F', L"Ctrl+Shift+F" },
        { ID_HOTKEY_KILL_MYTHWARE,    MOD_CONTROL | MOD_SHIFT, 'K', L"Ctrl+Shift+K" },
        { ID_HOTKEY_BROADCAST_WIN,    MOD_CONTROL | MOD_SHIFT, 'W', L"Ctrl+Shift+W" },
        { ID_HOTKEY_EXIT_BLACK,       MOD_CONTROL | MOD_SHIFT, 'X', L"Ctrl+Shift+X" },
        { ID_HOTKEY_SUSPEND_RESUME,   MOD_CONTROL | MOD_SHIFT, 'M', L"Ctrl+Shift+M" },
        { ID_HOTKEY_KILL_CLASSROOM,   MOD_CONTROL | MOD_SHIFT, 'C', L"Ctrl+Shift+C" },
        { ID_HOTKEY_UNBLOCK_ALL,      MOD_CONTROL | MOD_SHIFT, 'U', L"Ctrl+Shift+U" },
        { ID_HOTKEY_RESTART_EXPLORER, MOD_CONTROL | MOD_SHIFT, 'R', L"Ctrl+Shift+R" },
    };
    for (auto& k : keys) {
        if (!RegisterHotKey(hWnd, k.id, k.mod, k.vk)) {
            logger::Warn(std::wstring(L"快捷键注册失败: ") + k.name +
                        L" (可能被其他程序占用)");
        }
    }
    logger::Info(L"快捷键注册完成");
}

void UnregisterAll(HWND hWnd)
{
    UnregisterHotKey(hWnd, ID_HOTKEY_SHOW_MAIN);
    UnregisterHotKey(hWnd, ID_HOTKEY_HIDE_CURRENT);
    UnregisterHotKey(hWnd, ID_HOTKEY_SELECT);
    UnregisterHotKey(hWnd, ID_HOTKEY_PREVIEW);
    UnregisterHotKey(hWnd, ID_HOTKEY_FLOAT);
    UnregisterHotKey(hWnd, ID_HOTKEY_KILL_MYTHWARE);
    UnregisterHotKey(hWnd, ID_HOTKEY_BROADCAST_WIN);
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