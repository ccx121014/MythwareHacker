// window_utils.h - 窗口工具函数
#pragma once
#include "common.h"

namespace wutil {

// 获取窗口标题
std::wstring GetWindowTitle(HWND hwnd);

// 获取窗口所属进程名（去掉 .exe 后缀）
std::wstring GetProcessName(HWND hwnd);

// 获取窗口所属进程 PID
DWORD GetProcessIdFromHwnd(HWND hwnd);

// 判断窗口是否可用于隐蔽（可见、非子窗口、非系统窗口）
BOOL IsWindowEligible(HWND hwnd);

// 判断点是否在窗口标题栏上
bool IsPointOnTitleBar(POINT pt, HWND* outHwnd);

// 窗口信息结构
struct WindowInfo {
    HWND hwnd;
    std::wstring title;
    std::wstring processName;
    bool hidden;
};

// 枚举所有可见顶层窗口（排除桌面、任务栏等）
std::vector<WindowInfo> EnumAllTopLevelWindows();

// 按进程名查找所有窗口
std::vector<HWND> FindWindowsByProcessName(const std::wstring& processName);

// 按窗口标题精确查找（FindWindow 封装）
HWND FindWindowByTitle(const std::wstring& title);

} // namespace wutil
