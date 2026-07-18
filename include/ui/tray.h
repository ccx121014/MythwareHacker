// tray.h - 系统托盘图标 + 右键菜单
#pragma once
#include "common.h"

namespace tray {

// 添加托盘图标
void Add(HWND hWnd);

// 移除托盘图标
void Remove(HWND hWnd);

// 更新托盘 tooltip（显示可见/隐蔽窗口数和列表）
void UpdateTip(HWND hWnd);

// 显示托盘右键菜单（集成所有功能）
void ShowContextMenu(HWND hWnd);

} // namespace tray
