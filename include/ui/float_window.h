// float_window.h - 圆形悬浮窗
//
// 始终置顶的小圆窗：
//   左键：切换主面板（托盘菜单）
//   中键：一键广播窗口化
//   右键：快捷菜单（防截屏）
//   支持拖拽
#pragma once
#include "common.h"

namespace floatw {

// 注册窗口类
ATOM RegisterClass(HINSTANCE hInst);

// 显示悬浮窗
void Show();

// 隐藏悬浮窗
void Hide();

// 切换显示/隐藏
void Toggle();

// 是否可见
bool IsVisible();

// 清理
void Cleanup();

} // namespace floatw
