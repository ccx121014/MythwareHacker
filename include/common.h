// common.h
// MythwareHacker - 公共头文件
// 集大成：JiYuTrainer + MythwareToolkit + MythwareHide 三项目功能整合
//
// 兼容性：Win7+ 全兼容，32/64 双架构
//   - Win10 2004+：WDA_EXCLUDEFROMCAPTURE 全功能
//   - Win7/8/旧Win10：自动降级到屏幕外方案
//   - 32位/64位：注入时严格位数匹配

#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601  // Win7
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdio>

// ---------------------------------------------------------------------------
// 整数转宽字符串
// MinGW-w64 的 std::to_wstring 有已知 bug（对 long long/int 都可能输出乱码）
// 用 swprintf 替代，安全可靠
// ---------------------------------------------------------------------------
inline std::wstring WSTR(int v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%d", v); return std::wstring(buf);
}
inline std::wstring WSTR(unsigned int v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%u", v); return std::wstring(buf);
}
inline std::wstring WSTR(long v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%ld", v); return std::wstring(buf);
}
inline std::wstring WSTR(unsigned long v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%lu", v); return std::wstring(buf);
}
inline std::wstring WSTR(long long v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%lld", v); return std::wstring(buf);
}
inline std::wstring WSTR(unsigned long long v)
{
    wchar_t buf[32]; swprintf(buf, 32, L"%llu", v); return std::wstring(buf);
}

// ---------------------------------------------------------------------------
// 版本检测：运行时判断 WDA 是否可用（Win10 2004+ Build 19041）
// ---------------------------------------------------------------------------
namespace common {

inline bool IsWdaExcludeFromCaptureSupported()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return false;
    return GetProcAddress(hUser32, "SetWindowDisplayAffinity") != nullptr;
}

inline bool IsWow64()
{
    BOOL isWow64 = FALSE;
    return IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64;
}

inline bool IsSelf64Bit()
{
    return sizeof(void*) == 8;
}

inline bool IsProcess64Bit(HANDLE hProcess)
{
    // 如果本进程是 32 位，目标进程不可能是 64 位
    if (!IsSelf64Bit()) return false;
    // 本进程是 64 位，用 IsWow64Process 判断目标是否为 32 位 WoW64 进程
    BOOL wow64 = FALSE;
    if (IsWow64Process(hProcess, &wow64)) {
        return !wow64;
    }
    return false;
}

// 系统 Build 号（用于判断 WDA 支持）
inline DWORD GetSystemBuildNumber()
{
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    // GetVersionEx 在 Win10 上可能被 lie，用 RtlGetVersion 更可靠
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOEXW*);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pfn = (RtlGetVersion_t)GetProcAddress(hNtdll, "RtlGetVersion");
        if (pfn && pfn(&osvi) == 0) {
            return osvi.dwBuildNumber;
        }
    }
    return 0;
}

inline bool IsWin10Build19041OrLater()
{
    return GetSystemBuildNumber() >= 19041;
}

} // namespace common

// ---------------------------------------------------------------------------
// WDA 常量（旧 SDK 可能未定义）
// ---------------------------------------------------------------------------
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// DWM Cloak（Win8+）
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// ---------------------------------------------------------------------------
// 全局应用常量
// ---------------------------------------------------------------------------
#define APP_NAME                 L"MythwareHacker"
#define APP_TITLE                L"极域黑客工具"
#define APP_VERSION              L"2.1.0"
#define APP_MUTEX_NAME           L"MythwareHacker_Mutex_v2"
#define APP_MAIN_CLASS           L"MythwareHackerMainClass"
#define APP_PREVIEW_CLASS        L"MythwareHackerPreviewClass"
#define APP_FLOAT_CLASS          L"MythwareHackerFloatClass"

#define WM_TRAYICON              (WM_USER + 1)
#define WM_TITLEBAR_RIGHTCLICK   (WM_USER + 2)
#define WM_FLOAT_CLICK           (WM_USER + 3)

// 托盘菜单命令 ID 段
#define ID_TRAY_ICON             1000
#define ID_TRAY_HIDE_CURRENT     3000
#define ID_TRAY_SELECT_MODE      3001
#define ID_TRAY_RESTORE_ALL      3002
#define ID_TRAY_KILL_MYTHWARE    3003
#define ID_TRAY_SUSPEND_MYTHWARE 3004
#define ID_TRAY_RESUME_MYTHWARE  3005
#define ID_TRAY_BROADCAST_WIN    3006
#define ID_TRAY_BROADCAST_FULL   3007
#define ID_TRAY_EXIT_BLACK       3008
#define ID_TRAY_UNBLOCK_NET      3009
#define ID_TRAY_UNBLOCK_USB      3010
#define ID_TRAY_KILL_CLASSROOM   3011
#define ID_TRAY_CALC_PASSWORD    3012
#define ID_TRAY_FLOAT_TOGGLE     3013
#define ID_TRAY_PREVIEW          3014
#define ID_TRAY_RESTORE_SYS      3015
#define ID_TRAY_SHOW_GUI         3016
#define ID_TRAY_RESTART_EXPLORER 3017
#define ID_TRAY_BROADCAST_TOPMOST 3018  // 广播置顶开关
#define ID_TRAY_EXIT             3999
#define ID_TRAY_WINDOW_LIST_BASE 5000

// 标题栏菜单
#define ID_TITLEBAR_HIDE         2001
#define ID_TITLEBAR_RESTORE      2002
#define ID_TITLEBAR_CANCEL       2003

// 定时器
#define TRAYTIP_TIMER_ID         1
#define TRAYTIP_INTERVAL_MS      3000
#define PREVIEW_TIMER_ID         2
#define PREVIEW_INTERVAL_MS      500
#define FLOAT_TIMER_ID           3
#define FLOAT_INTERVAL_MS        1000
