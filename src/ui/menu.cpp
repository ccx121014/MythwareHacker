// menu.cpp - 菜单构建实现
#include "ui/menu.h"
#include "ui/app_state.h"
#include "core/window_hide.h"
#include "core/process_control.h"
#include "core/driver_control.h"
#include "core/mythware_control.h"
#include "core/password_calc.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace menu {

// 全局：菜单项到窗口句柄的映射（用于窗口列表菜单）
static std::vector<HWND> g_menuWindowMap;

void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);

    HMENU hMenu = CreatePopupMenu();

    // === 主界面入口 ===
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW_GUI, L"打开主界面");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // === 窗口隐蔽区 ===
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_HIDE_CURRENT,
        L"隐蔽/恢复当前窗口\tCtrl+Shift+H");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SELECT_MODE,
        L"窗口选择模式\tCtrl+Shift+S");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_PREVIEW,
        L"截图预览（模拟教师端视角）\tCtrl+Shift+P");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_FLOAT_TOGGLE,
        L"显示/隐藏悬浮窗\tCtrl+Shift+F");

    if (!whide::GetAll().empty()) {
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE_ALL, L"恢复所有隐蔽窗口");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // === 极域进程控制区 ===
    auto status = pctl::GetMythwareStatus();
    std::wstring stateText = L"极域状态: ";
    switch (status.state) {
    case pctl::MythwareState::NotRunning:  stateText += L"未运行"; break;
    case pctl::MythwareState::Running:     stateText += L"运行中 (PID:" + WSTR(status.pid) + L")"; break;
    case pctl::MythwareState::Suspended:   stateText += L"已挂起"; break;
    case pctl::MythwareState::NoResponse:  stateText += L"无响应"; break;
    }
    if (!status.version.empty()) stateText += L" v" + status.version;
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, stateText.c_str());

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_KILL_MYTHWARE,   L"强杀极域进程");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SUSPEND_MYTHWARE,L"挂起（冻结）极域");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESUME_MYTHWARE, L"恢复极域");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_BROADCAST_WIN,   L"广播窗口化");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_BROADCAST_FULL,  L"广播全屏化");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT_BLACK,      L"退出黑屏安静");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // === 限制解除区 ===
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_UNBLOCK_NET, L"解除网络限制 (卸载 TDNetFilter)");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_UNBLOCK_USB, L"解除 U 盘限制 (卸载 TDFileFilter)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // === 学生机房管理助手区 ===
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_KILL_CLASSROOM, L"杀掉学生机房管理助手");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_CALC_PASSWORD,  L"动态密码计算器…");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE_SYS,    L"一键解禁系统程序 (CMD/注册表/任务管理器)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // === 窗口列表 ===
    whide::Cleanup();
    auto allWindows = wutil::EnumAllTopLevelWindows();
    for (auto& w : allWindows) w.hidden = whide::IsHidden(w.hwnd);

    if (!allWindows.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"所有窗口 (点击切换隐蔽):");
        g_menuWindowMap.clear();
        UINT menuId = ID_TRAY_WINDOW_LIST_BASE;

        for (const auto& w : allWindows) {
            std::wstring item = L"  ";
            if (w.hidden) {
                item += L"[隐] ";
                // 显示生效方案
                const auto& hidden = whide::GetAll();
                for (const auto& hw : hidden) {
                    if (hw.hwnd == w.hwnd) {
                        if (hw.offscreenOk)      item += L"[移] ";
                        else if (hw.wdaOk || hw.injectOk || hw.cloakOk) item += L"[截] ";
                        else                     item += L"[?] ";
                        break;
                    }
                }
            }
            item += w.title;
            if (item.length() > 45)
                item = item.substr(0, 44) + L"…";

            AppendMenuW(hMenu, MF_STRING, menuId, item.c_str());
            g_menuWindowMap.push_back(w.hwnd);
            menuId++;
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

void ShowTitleBarMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(app::g_ctx.hWndMain);

    bool hidden = whide::IsHidden(hwnd);
    std::wstring title = wutil::GetWindowTitle(hwnd);
    std::wstring process = wutil::GetProcessName(hwnd);

    HMENU hMenu = CreatePopupMenu();

    std::wstring info = title;
    if (!process.empty()) info += L"  |  " + process;
    if (!title.empty() && title != L"(无标题)")
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, info.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    if (hidden) {
        AppendMenuW(hMenu, MF_STRING, ID_TITLEBAR_RESTORE,
            L"恢复此窗口（教师端可见）");
    } else {
        AppendMenuW(hMenu, MF_STRING, ID_TITLEBAR_HIDE,
            L"隐蔽此窗口（教师端不可见）");
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TITLEBAR_CANCEL, L"取消");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, app::g_ctx.hWndMain, nullptr);
    DestroyMenu(hMenu);
}

// 根据菜单 ID 获取对应窗口（供 WndProc 调用）
HWND GetWindowFromMenuId(UINT menuId)
{
    size_t idx = menuId - ID_TRAY_WINDOW_LIST_BASE;
    if (idx < g_menuWindowMap.size()) {
        return g_menuWindowMap[idx];
    }
    return nullptr;
}

void ShowPasswordCalculator(HWND hWndParent)
{
    // 简化：用 InputBox 风格，弹多个 MessageBox 收集输入
    // 实际项目可用对话框资源，这里用简化版本

    // 输入版本号
    wchar_t version[64] = L"11.06";
    // 由于没有对话框资源，用简化方式：直接用当前日期和默认版本计算
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstring computerName = pwcalc::GetLocalComputerName();
    std::wstring pwd = pwcalc::CalculateAuto(version, st.wYear, st.wMonth, st.wDay, computerName);

    std::wstring msg = L"动态密码计算器\n\n"
                       L"版本: 11.06 (默认)\n"
                       L"日期: " + WSTR(st.wYear) + L"-" +
                       WSTR(st.wMonth) + L"-" + WSTR(st.wDay) + L"\n"
                       L"计算机名: " + computerName + L"\n\n"
                       L"计算结果（临时密码）:\n  " + pwd + L"\n\n"
                       L"如需其他版本，请修改源码中的版本号。";
    MessageBoxW(hWndParent, msg.c_str(), L"动态密码计算器", MB_OK | MB_ICONINFORMATION);
    logger::Info(L"密码计算: 版本=11.06 结果=" + pwd);
}

} // namespace menu
