// menu.h - 菜单构建（托盘右键菜单 + 标题栏右键菜单）
#pragma once
#include "common.h"

namespace menu {

// 托盘右键菜单（集成所有功能模块）
void ShowTrayMenu(HWND hWnd);

// 标题栏右键菜单（隐蔽/恢复当前窗口）
void ShowTitleBarMenu(HWND hwnd);

// 密码计算器对话框
void ShowPasswordCalculator(HWND hWndParent);

// 根据菜单 ID 获取对应窗口句柄（供 WndProc 调用）
HWND GetWindowFromMenuId(UINT menuId);

} // namespace menu
