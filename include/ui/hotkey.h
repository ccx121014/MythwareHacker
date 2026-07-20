// hotkey.h - 全局快捷键
#pragma once
#include "common.h"

namespace hotkey {

// 快捷键 ID
enum {
    ID_HOTKEY_HIDE          = 100,
    ID_HOTKEY_SELECT        = 101,
    ID_HOTKEY_PREVIEW       = 102,
    ID_HOTKEY_FLOAT         = 103,
    ID_HOTKEY_KILL_MYTHWARE = 104,
    ID_HOTKEY_SHOW_MAIN     = 105,  // Alt+B 唤起主窗口
};

// 注册所有全局快捷键
void RegisterAll(HWND hWnd);

// 注销所有快捷键
void UnregisterAll(HWND hWnd);

// 处理快捷键消息，返回 true 表示已处理
bool Handle(WPARAM wParam);

} // namespace hotkey