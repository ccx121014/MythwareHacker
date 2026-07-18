// hotkey.cpp - 全局快捷键实现
#include "ui/hotkey.h"
#include "utils/log.h"

namespace hotkey {

void RegisterAll(HWND hWnd)
{
    struct { int id; UINT mod; UINT vk; const wchar_t* name; } keys[] = {
        { ID_HOTKEY_HIDE,          MOD_CONTROL | MOD_SHIFT, 'H', L"Ctrl+Shift+H" },
        { ID_HOTKEY_SELECT,        MOD_CONTROL | MOD_SHIFT, 'S', L"Ctrl+Shift+S" },
        { ID_HOTKEY_PREVIEW,       MOD_CONTROL | MOD_SHIFT, 'P', L"Ctrl+Shift+P" },
        { ID_HOTKEY_FLOAT,         MOD_CONTROL | MOD_SHIFT, 'F', L"Ctrl+Shift+F" },
        { ID_HOTKEY_KILL_MYTHWARE, MOD_CONTROL | MOD_SHIFT, 'K', L"Ctrl+Shift+K" },
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
}

} // namespace hotkey
