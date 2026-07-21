// hotkey.h - 全局快捷键
#pragma once
#include "common.h"

namespace hotkey {

// 快捷键 ID
enum {
    ID_HOTKEY_SHOW_MAIN     = 100,  // Alt+B 唤起主窗口
    ID_HOTKEY_HIDE_CURRENT  = 101,  // Ctrl+Shift+H 隐蔽当前窗口
    ID_HOTKEY_SELECT        = 102,  // Ctrl+Shift+S 窗口选择模式
    ID_HOTKEY_PREVIEW       = 103,  // Ctrl+Shift+P 截图预览
    ID_HOTKEY_FLOAT         = 104,  // Ctrl+Shift+F 悬浮窗
    ID_HOTKEY_KILL_MYTHWARE = 105,  // Ctrl+Shift+K 强杀极域
    ID_HOTKEY_BROADCAST_WIN = 106,  // Ctrl+Shift+W 广播窗口化
    ID_HOTKEY_EXIT_BLACK    = 107,  // Ctrl+Shift+B 退出黑屏
    ID_HOTKEY_SUSPEND_RESUME= 108,  // Ctrl+Shift+M 挂起/恢复极域
    ID_HOTKEY_KILL_CLASSROOM= 109,  // Ctrl+Shift+C 杀掉机房助手
    ID_HOTKEY_UNBLOCK_ALL   = 110,  // Ctrl+Shift+U 一键解除全部
    ID_HOTKEY_RESTART_EXPLORER = 111, // Ctrl+Shift+R 重启资源管理器
};

// 注册所有全局快捷键
void RegisterAll(HWND hWnd);

// 注销所有快捷键
void UnregisterAll(HWND hWnd);

// 处理快捷键消息，返回 true 表示已处理
bool Handle(WPARAM wParam);

} // namespace hotkey