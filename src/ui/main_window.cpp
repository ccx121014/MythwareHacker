// main_window.cpp - 图形界面主窗口实现
#include "ui/main_window.h"
#include "ui/tray.h"
#include "ui/float_window.h"
#include "ui/preview.h"
#include "core/window_hide.h"
#include "core/process_control.h"
#include "core/driver_control.h"
#include "core/mythware_control.h"
#include "core/password_calc.h"
#include "utils/log.h"
#include "utils/window_utils.h"
#include <cstdio>

namespace mainwin {

static HWND g_hWnd    = nullptr;
static HWND g_hList   = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hLog    = nullptr;
static HFONT g_hFont  = nullptr;
static std::wstring g_logText;

// ---------------------------------------------------------------------------
// 辅助：创建控件
// ---------------------------------------------------------------------------
static HWND MkBtn(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h)
{
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return btn;
}

static HWND MkLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    HWND lbl = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(lbl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return lbl;
}

static HWND MkEdit(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL)
{
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        style, x, y, w, h, parent, (HMENU)(INT_PTR)id, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return edit;
}

static HWND MkGroup(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    HWND grp = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, parent, nullptr, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(grp, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return grp;
}

// ---------------------------------------------------------------------------
// 格式化极域状态文本
// ---------------------------------------------------------------------------
static std::wstring FormatMythwareStatus()
{
    auto st = pctl::GetMythwareStatus();
    std::wstring stateText;
    switch (st.state) {
    case pctl::MythwareState::NotRunning:  stateText = L"未运行"; break;
    case pctl::MythwareState::Running:     stateText = L"运行中"; break;
    case pctl::MythwareState::Suspended:   stateText = L"已挂起"; break;
    case pctl::MythwareState::NoResponse:  stateText = L"无响应"; break;
    }
    std::wstring s = L"极域状态: " + stateText;
    if (st.pid) s += L"  PID: " + std::to_wstring(st.pid);
    if (!st.version.empty()) s += L"  版本: " + st.version;
    s += L"  |  系统: " + std::wstring(common::IsWin10Build19041OrLater() ? L"Win10 2004+" : L"旧系统");
    s += L"  " + std::wstring(common::IsSelf64Bit() ? L"64位" : L"32位");
    return s;
}

// ---------------------------------------------------------------------------
// 密码计算
// ---------------------------------------------------------------------------
static void DoCalcPassword(HWND hWnd)
{
    wchar_t ver[64] = {}, date[64] = {}, pcname[256] = {};
    GetDlgItemTextW(hWnd, IDC_EDIT_VERSION, ver, 64);
    GetDlgItemTextW(hWnd, IDC_EDIT_DATE, date, 64);
    GetDlgItemTextW(hWnd, IDC_EDIT_PCNAME, pcname, 256);

    int year = 0, month = 0, day = 0;
    if (swscanf(date, L"%d-%d-%d", &year, &month, &day) != 3) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        year = st.wYear; month = st.wMonth; day = st.wDay;
    }

    std::wstring cn = pcname;
    if (cn.empty()) cn = pwcalc::GetLocalComputerName();

    std::wstring verStr = ver;
    if (verStr.empty()) verStr = L"11.06";

    std::wstring pwd = pwcalc::CalculateAuto(verStr, year, month, day, cn);

    std::wstring result = L"版本: " + verStr + L"\n" +
                          L"日期: " + std::to_wstring(year) + L"-" +
                          std::to_wstring(month) + L"-" + std::to_wstring(day) + L"\n" +
                          L"计算机名: " + cn + L"\n" +
                          L"密码: " + pwd;
    std::wstring mythPwd = pwcalc::ReadMythwarePassword();
    if (!mythPwd.empty()) {
        result += L"\n极域注册表密码: " + mythPwd;
    }
    SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, result.c_str());
    AppendLog(L"密码计算: " + verStr + L" → " + pwd);
}

// ---------------------------------------------------------------------------
// 恢复选中窗口
// ---------------------------------------------------------------------------
static void DoRestoreSelected(HWND hWnd)
{
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
        MessageBoxW(hWnd, L"请先在列表中选择一个窗口", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto& hidden = whide::GetAll();
    if (sel < (int)hidden.size()) {
        whide::Restore(hidden[sel].hwnd);
        AppendLog(L"已恢复: " + hidden[sel].title);
    }
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {

    case WM_CREATE: {
        g_hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        // 顶部状态栏
        g_hStatus = MkLabel(hWnd, L"极域状态: 检测中...", 10, 5, 680, 22);
        MkBtn(hWnd, L"刷新", IDC_BTN_REFRESH, 700, 3, 80, 24);

        // ===== 左侧 =====

        // 窗口隐蔽
        MkGroup(hWnd, L"窗口隐蔽", 10, 30, 390, 225);
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            20, 52, 370, 120, hWnd, (HMENU)IDC_LIST_HIDDEN, app::g_ctx.hInst, nullptr);
        if (g_hFont) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        MkBtn(hWnd, L"隐藏当前", IDC_BTN_HIDE_CURRENT, 20, 178, 120, 28);
        MkBtn(hWnd, L"恢复选中", IDC_BTN_RESTORE_SEL, 145, 178, 120, 28);
        MkBtn(hWnd, L"恢复全部", IDC_BTN_RESTORE_ALL, 270, 178, 120, 28);
        MkBtn(hWnd, L"选择模式", IDC_BTN_SELECT_MODE, 20, 210, 120, 28);
        MkBtn(hWnd, L"截图预览", IDC_BTN_PREVIEW, 145, 210, 120, 28);
        MkBtn(hWnd, L"悬浮窗",   IDC_BTN_FLOAT, 270, 210, 120, 28);

        // 系统操作
        MkGroup(hWnd, L"系统操作", 10, 265, 390, 70);
        MkBtn(hWnd, L"杀机房助手",   IDC_BTN_KILL_CLASSROOM, 20, 288, 120, 28);
        MkBtn(hWnd, L"解禁系统程序", IDC_BTN_RESTORE_SYS, 145, 288, 120, 28);
        MkBtn(hWnd, L"重启资源管理器",IDC_BTN_RESTART_EXPLORER, 270, 288, 120, 28);

        // ===== 右侧 =====

        // 极域控制
        MkGroup(hWnd, L"极域控制", 410, 30, 420, 170);
        MkBtn(hWnd, L"强杀极域",   IDC_BTN_KILL_MYTH,      420, 52, 130, 28);
        MkBtn(hWnd, L"挂起极域",   IDC_BTN_SUSPEND,        555, 52, 130, 28);
        MkBtn(hWnd, L"恢复极域",   IDC_BTN_RESUME,         690, 52, 130, 28);
        MkBtn(hWnd, L"启动极域",   IDC_BTN_START_MYTH,     420, 84, 130, 28);
        MkBtn(hWnd, L"广播窗口化", IDC_BTN_BROADCAST_WIN,  555, 84, 130, 28);
        MkBtn(hWnd, L"广播全屏化", IDC_BTN_BROADCAST_FULL, 690, 84, 130, 28);
        MkBtn(hWnd, L"退出黑屏",   IDC_BTN_EXIT_BLACK,     420, 116, 130, 28);

        // 限制解除
        MkGroup(hWnd, L"限制解除", 410, 210, 420, 90);
        MkBtn(hWnd, L"解除网络限制",   IDC_BTN_UNBLOCK_NET, 420, 233, 130, 28);
        MkBtn(hWnd, L"解除U盘限制",    IDC_BTN_UNBLOCK_USB, 555, 233, 130, 28);
        MkBtn(hWnd, L"一键解除全部",   IDC_BTN_UNBLOCK_ALL, 690, 233, 130, 28);
        MkBtn(hWnd, L"解除键盘锁",     IDC_BTN_UNBLOCK_KEYBD, 420, 265, 130, 28);

        // 密码计算器
        MkGroup(hWnd, L"动态密码计算器", 410, 310, 420, 175);
        MkLabel(hWnd, L"版本号:",   420, 334, 55, 20);
        MkEdit(hWnd, L"11.06", IDC_EDIT_VERSION, 480, 332, 155, 22);
        MkLabel(hWnd, L"日期:",     420, 362, 55, 20);
        {
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t dateBuf[32];
            swprintf(dateBuf, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            MkEdit(hWnd, dateBuf, IDC_EDIT_DATE, 480, 360, 155, 22);
        }
        MkLabel(hWnd, L"计算机名:", 420, 390, 55, 20);
        MkEdit(hWnd, pwcalc::GetLocalComputerName().c_str(), IDC_EDIT_PCNAME, 480, 388, 155, 22);
        MkBtn(hWnd, L"计算密码", IDC_BTN_CALC_PASSWORD, 645, 386, 175, 28);
        MkBtn(hWnd, L"读取极域密码", IDC_BTN_READ_MYTH_PWD, 645, 416, 175, 28);
        MkLabel(hWnd, L"计算结果:", 420, 425, 70, 20);
        MkEdit(hWnd, L"", IDC_EDIT_RESULT, 420, 445, 395, 30,
               WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);

        // 操作日志
        MkGroup(hWnd, L"操作日志", 10, 495, 820, 90);
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL,
            20, 517, 800, 55, hWnd, (HMENU)IDC_EDIT_LOG, app::g_ctx.hInst, nullptr);
        if (g_hFont) SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 定时刷新
        SetTimer(hWnd, IDC_GUI_TIMER, 3000, nullptr);

        // 初始化
        RefreshStatus();
        RefreshWindowList();
        AppendLog(L"图形界面已启动");
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDC_GUI_TIMER) {
            RefreshStatus();
            RefreshWindowList();
        }
        return 0;

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);

        // 窗口隐蔽
        if (cmd == IDC_BTN_HIDE_CURRENT) {
            std::wstring diag;
            whide::ToggleCurrent();
            RefreshWindowList();
            tray::UpdateTip(app::g_ctx.hWndMain);
        } else if (cmd == IDC_BTN_RESTORE_SEL) {
            DoRestoreSelected(hWnd);
            RefreshWindowList();
            tray::UpdateTip(app::g_ctx.hWndMain);
        } else if (cmd == IDC_BTN_RESTORE_ALL) {
            whide::RestoreAll();
            RefreshWindowList();
            tray::UpdateTip(app::g_ctx.hWndMain);
            AppendLog(L"已恢复所有隐蔽窗口");
        } else if (cmd == IDC_BTN_SELECT_MODE) {
            app::g_ctx.selectMode = true;
            SetCursor(app::g_ctx.hCursorCross);
            ShowWindow(hWnd, SW_HIDE);
            AppendLog(L"已进入选择模式（左键隐蔽/恢复，右键退出）");
        } else if (cmd == IDC_BTN_PREVIEW) {
            preview::Toggle();
        } else if (cmd == IDC_BTN_FLOAT) {
            floatw::Toggle();
        }

        // 系统操作
        else if (cmd == IDC_BTN_KILL_CLASSROOM) {
            auto r = mctl::KillClassroomHelper();
            AppendLog(L"杀机房助手: " + std::to_wstring(r.killedCount) + L" 个进程");
        } else if (cmd == IDC_BTN_RESTORE_SYS) {
            auto r = mctl::UnblockSystemPrograms();
            AppendLog(L"解禁系统程序: " + r.detail);
        } else if (cmd == IDC_BTN_RESTART_EXPLORER) {
            if (MessageBoxW(hWnd, L"确认重启资源管理器？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                mctl::RestartExplorer();
                AppendLog(L"已重启资源管理器");
            }
        }

        // 极域控制
        else if (cmd == IDC_BTN_KILL_MYTH) {
            if (MessageBoxW(hWnd, L"确认强杀极域进程？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                pctl::KillMythware();
                AppendLog(L"已强杀极域进程");
            }
        } else if (cmd == IDC_BTN_SUSPEND) {
            pctl::SuspendMythware();
            AppendLog(L"已挂起极域进程");
        } else if (cmd == IDC_BTN_RESUME) {
            pctl::ResumeMythware();
            AppendLog(L"已恢复极域进程");
        } else if (cmd == IDC_BTN_START_MYTH) {
            pctl::StartMythware();
            AppendLog(L"已尝试启动极域");
        } else if (cmd == IDC_BTN_BROADCAST_WIN) {
            bool ok = pctl::BroadcastToWindowed();
            AppendLog(ok ? L"已将广播窗口化" : L"未找到广播窗口");
        } else if (cmd == IDC_BTN_BROADCAST_FULL) {
            bool ok = pctl::BroadcastToFullscreen();
            AppendLog(ok ? L"已将广播全屏化" : L"操作失败");
        } else if (cmd == IDC_BTN_EXIT_BLACK) {
            bool ok = pctl::ExitBlackScreen();
            AppendLog(ok ? L"已退出黑屏" : L"未找到黑屏窗口");
        }

        // 限制解除
        else if (cmd == IDC_BTN_UNBLOCK_NET) {
            if (MessageBoxW(hWnd, L"确认卸载 TDNetFilter 驱动？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockNetwork();
                AppendLog(ok ? L"网络限制已解除" : L"网络限制解除失败");
            }
        } else if (cmd == IDC_BTN_UNBLOCK_USB) {
            if (MessageBoxW(hWnd, L"确认卸载 TDFileFilter 驱动？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockUSB();
                AppendLog(ok ? L"U盘限制已解除" : L"U盘限制解除失败");
            }
        } else if (cmd == IDC_BTN_UNBLOCK_ALL) {
            if (MessageBoxW(hWnd, L"确认一键卸载所有限制驱动？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                auto r = drvctl::UnblockAll();
                AppendLog(r.detail);
            }
        } else if (cmd == IDC_BTN_UNBLOCK_KEYBD) {
            bool ok = drvctl::UnblockKeyboard();
            AppendLog(ok ? L"键盘锁已解除" : L"键盘锁解除失败（可能未安装）");
        }

        // 密码计算
        else if (cmd == IDC_BTN_CALC_PASSWORD) {
            DoCalcPassword(hWnd);
        }
        else if (cmd == IDC_BTN_READ_MYTH_PWD) {
            std::wstring pwd = pwcalc::ReadMythwarePassword();
            if (pwd.empty()) {
                SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, L"未找到极域密码（注册表中无 Knock1 值）");
                AppendLog(L"读取极域密码: 未找到");
            } else {
                SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, (L"极域注册表密码: " + pwd).c_str());
                AppendLog(L"读取极域密码: " + pwd);
            }
        }

        // 刷新
        else if (cmd == IDC_BTN_REFRESH) {
            RefreshStatus();
            RefreshWindowList();
            AppendLog(L"已刷新状态");
        }
        return 0;
    }

    case WM_CLOSE:
        // 隐藏到托盘，不退出
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDC_GUI_TIMER);
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        g_hWnd = nullptr;
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 860;
        mmi->ptMinTrackSize.y = 640;
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------

bool RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_GUI_CLASS;
    wc.hIcon         = LoadIcon(nullptr, IDI_SHIELD);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    return RegisterClassExW(&wc) != 0;
}

HWND Create(HINSTANCE hInst)
{
    g_hWnd = CreateWindowExW(
        0,
        APP_GUI_CLASS,
        APP_TITLE L" - 集大成版",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 640,
        nullptr, nullptr, hInst, nullptr);
    return g_hWnd;
}

void Show()
{
    if (g_hWnd) {
        ShowWindow(g_hWnd, SW_SHOW);
        SetForegroundWindow(g_hWnd);
        RefreshStatus();
        RefreshWindowList();
    }
}

void Hide()
{
    if (g_hWnd) ShowWindow(g_hWnd, SW_HIDE);
}

void Toggle()
{
    if (!g_hWnd) return;
    if (IsWindowVisible(g_hWnd))
        ShowWindow(g_hWnd, SW_HIDE);
    else
        Show();
}

bool IsVisible()
{
    return g_hWnd && IsWindowVisible(g_hWnd);
}

void RefreshStatus()
{
    if (!g_hStatus) return;
    SetWindowTextW(g_hStatus, FormatMythwareStatus().c_str());
}

void RefreshWindowList()
{
    if (!g_hList) return;
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    const auto& hidden = whide::GetAll();
    for (const auto& hw : hidden) {
        std::wstring item = hw.title + L"  [" + hw.processName + L"]";
        if (hw.wdaOk)       item += L" WDA";
        else if (hw.injectOk) item += L" 注入";
        else if (hw.cloakOk)  item += L" Cloak";
        else if (hw.offscreenOk) item += L" 屏幕外";
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

void AppendLog(const std::wstring& text)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[32];
    swprintf(ts, 32, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    g_logText = ts + text + L"\r\n" + g_logText;
    // 限制日志长度
    if (g_logText.size() > 3000) {
        g_logText = g_logText.substr(0, 3000);
    }
    if (g_hLog) SetWindowTextW(g_hLog, g_logText.c_str());
    logger::Info(text);
}

} // namespace mainwin
