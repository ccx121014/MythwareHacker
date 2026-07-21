// main_window.h - 图形界面主窗口
//
// 提供完整的图形界面，整合所有功能模块：
//   窗口隐蔽、极域控制、限制解除、密码计算器、系统解禁
#pragma once
#include "common.h"
#include "ui/app_state.h"

#define APP_GUI_CLASS L"MythwareHackerGUIClass"

// 主窗口控件 ID（10000+ 段，不与托盘菜单 ID 冲突）
#define IDC_BTN_HIDE_CURRENT     10001
#define IDC_BTN_RESTORE_SEL      10002
#define IDC_BTN_RESTORE_ALL      10003
#define IDC_BTN_SELECT_MODE      10004
#define IDC_BTN_PREVIEW          10005
#define IDC_BTN_FLOAT            10006
#define IDC_BTN_KILL_CLASSROOM   10010
#define IDC_BTN_RESTORE_SYS      10011
#define IDC_BTN_RESTART_EXPLORER 10012
#define IDC_BTN_KILL_MYTH        10020
#define IDC_BTN_SUSPEND          10021
#define IDC_BTN_RESUME           10022
#define IDC_BTN_START_MYTH       10023
#define IDC_BTN_BROADCAST_WIN    10024
#define IDC_BTN_BROADCAST_FULL   10025
#define IDC_BTN_EXIT_BLACK       10026
#define IDC_BTN_UNBLOCK_NET      10030
#define IDC_BTN_UNBLOCK_USB      10031
#define IDC_BTN_UNBLOCK_ALL      10032
#define IDC_BTN_UNBLOCK_KEYBD    10033
#define IDC_BTN_CALC_PASSWORD    10040
#define IDC_EDIT_VERSION         10041
#define IDC_EDIT_DATE            10042
#define IDC_EDIT_PCNAME          10043
#define IDC_EDIT_RESULT         10044
#define IDC_BTN_READ_MYTH_PWD   10045
#define IDC_LIST_HIDDEN          10050
#define IDC_STATIC_STATUS        10060
#define IDC_EDIT_LOG             10061
#define IDC_BTN_REFRESH          10070
#define IDC_CHK_AUTOSTART        10080
#define IDC_GUI_TIMER            200

namespace mainwin {

bool RegisterClass(HINSTANCE hInst);
HWND Create(HINSTANCE hInst);
void Show();
void Hide();
void Toggle();
bool IsVisible();
void RefreshStatus();
void RefreshWindowList();
void AppendLog(const std::wstring& text);

} // namespace mainwin
