// float_window.h - 圆形悬浮窗（状态实时刷新）
//
// 始终置顶的小圆窗，每1.5秒刷新状态：
//   颜色：灰色=极域未运行  蓝色=运行中  橙色=已挂起
//   文字：·=未运行  M=运行无广播  W=广播窗口化  F=广播全屏
//   金色外圈=广播置顶已开启
//
//   左键：托盘菜单
//   中键：切换广播全屏/窗口化
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
