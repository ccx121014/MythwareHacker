// hotkey.cpp - 全局快捷键实现
#include "ui/hotkey.h"
#include "ui/main_window.h"
#include "ui/float_window.h"
#include "ui/app_state.h"
#include "core/process_control.h"
#include "utils/log.h"

namespace hotkey {

void RegisterAll(HWND hWnd)
{
    struct { int id; UINT mod; UINT vk; const wchar_t* name; } keys[] = {
        // 原有快捷键
        { ID_HOTKEY_HIDE,          MOD_CONTROL | MOD_SHIFT, 'H', L"Ctrl+Shift+H" },
        { ID_HOTKEY_SELECT,        MOD_CONTROL | MOD_SHIFT, 'S', L"Ctrl+Shift+S" },
        { ID_HOTKEY_PREVIEW,       MOD_CONTROL | MOD_SHIFT, 'P', L"Ctrl+Shift+P" },
        { ID_HOTKEY_FLOAT,         MOD_CONTROL | MOD_SHIFT, 'F', L"Ctrl+Shift+F" },
        { ID_HOTKEY_KILL_MYTHWARE, MOD_CONTROL | MOD_SHIFT, 'K', L"Ctrl+Shift+K" },
        // 新增：Alt+B 唤起主窗口
        { ID_HOTKEY_SHOW_MAIN,     MOD_ALT,                'B', L"Alt+B" },
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
    UnregisterHotKey(hWnd, ID_HOTKEY_HIDE);
    UnregisterHotKey(hWnd, ID_HOTKEY_SELECT);
    UnregisterHotKey(hWnd, ID_HOTKEY_PREVIEW);
    UnregisterHotKey(hWnd, ID_HOTKEY_FLOAT);
    UnregisterHotKey(hWnd, ID_HOTKEY_KILL_MYTHWARE);
    UnregisterHotKey(hWnd, ID_HOTKEY_SHOW_MAIN);
}

bool Handle(WPARAM wParam)
{
    switch (wParam) {
    case ID_HOTKEY_HIDE:
        mainwin::Toggle();
        return true;
    case ID_HOTKEY_SELECT:
        app::g_ctx.selectMode = true;
        SetCursor(app::g_ctx.hCursorCross);
        ShowWindow(app::g_ctx.hWndMain, SW_HIDE);
        logger::Info(L"已进入选择模式");
        return true;
    case ID_HOTKEY_PREVIEW:
        // 预览功能通过 tray 触发
        return true;
    case ID_HOTKEY_FLOAT:
        floatw::Toggle();
        return true;
    case ID_HOTKEY_KILL_MYTHWARE:
        pctl::KillMythware();
        logger::Info(L"已强杀极域进程");
        return true;
    case ID_HOTKEY_SHOW_MAIN:
        mainwin::Show();
        return true;
    }
    return false;
}

} // namespace hotkey