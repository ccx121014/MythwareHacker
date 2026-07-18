// hotkey.h - 全局快捷键
#pragma once
#include "common.h"

namespace hotkey {

// 注册所有全局快捷键
void RegisterAll(HWND hWnd);

// 注销所有快捷键
void UnregisterAll(HWND hWnd);

} // namespace hotkey
