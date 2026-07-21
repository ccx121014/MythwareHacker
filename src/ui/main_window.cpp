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
#include <vector>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

namespace mainwin {

static HWND g_hWnd     = nullptr;
static HWND g_hList    = nullptr;
static HWND g_hStatus  = nullptr;
static HWND g_hVersion = nullptr;
static HWND g_hDate    = nullptr;
static HWND g_hPcName  = nullptr;
static HWND g_hResult  = nullptr;
static HFONT g_hFont   = nullptr;
static HANDLE g_hTopmostThread = nullptr;
static bool g_topmostRunning = false;

static DWORD WINAPI TopmostThreadProc(LPVOID)
{
    while (g_topmostRunning) {
        pctl::DemoteMythwareWindows();
        if (g_hWnd && IsWindow(g_hWnd) && IsWindowVisible(g_hWnd)) {
            SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        Sleep(500);
    }
    return 0;
}

static void StartTopmostThread()
{
    if (g_hTopmostThread) return;
    g_topmostRunning = true;
    g_hTopmostThread = CreateThread(nullptr, 0, TopmostThreadProc, nullptr, 0, nullptr);
}

static void StopTopmostThread()
{
    g_topmostRunning = false;
    if (g_hTopmostThread) {
        WaitForSingleObject(g_hTopmostThread, 1000);
        CloseHandle(g_hTopmostThread);
        g_hTopmostThread = nullptr;
    }
}

static const int WIN_W = 680;
static const int WIN_H = 500;

static BOOL CALLBACK SetFontEnumProc(HWND hwnd, LPARAM lParam)
{
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

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

    auto all = pwcalc::CalculateAll(year, month, day, cn);
    std::wstring result = L"日期: " + WSTR(year) + L"-" + WSTR(month) + L"-" + WSTR(day) +
                          L"  ·  计算机: " + cn + L"\r\n" +
                          L"10.1前:     " + all.preV10 + L"\r\n" +
                          L"10.x:       " + all.v10ToV11 + L"\r\n" +
                          L"11.0x:      " + all.v11ToV1106 + L"\r\n" +
                          L"11.06~12.0: " + all.v1106ToV12 + L"\r\n";

    std::wstring pwd = pwcalc::CalculateAuto(verStr, year, month, day, cn);
    result += L"\r\n当前版本 " + verStr + L" → " + pwd;

    std::wstring mythPwd = pwcalc::ReadMythwarePassword();
    if (!mythPwd.empty()) result += L"\r\n极域注册表密码: " + mythPwd;

    SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, result.c_str());
    AppendLog(L"密码计算: " + verStr + L" → " + pwd);
}

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

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {

    case WM_CREATE: {
        g_hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        const int L = 12;
        const int GAP = 12;
        const int LW = 320;
        const int RW = WIN_W - L - LW - GAP - L;
        const int R = L + LW + GAP;
        const int totalW = WIN_W - L * 2;
        int y;

        y = 10;
        // 左侧：极域控制
        CreateWindowW(L"BUTTON", L"极域控制", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            L, y, LW, 140, hWnd, nullptr, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"杀掉机房助手", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 10, y + 22, LW - 20, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_KILL_CLASSROOM, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"杀掉极域", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 10, y + 54, LW - 20, 32, hWnd, (HMENU)(INT_PTR)IDC_BTN_KILL_MYTH, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"挂起极域", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 10, y + 92, (LW - 20 - 8) / 3, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_SUSPEND, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"恢复极域", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 10 + (LW - 20 - 8) / 3 + 4, y + 92, (LW - 20 - 8) / 3, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_RESUME, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"启动极域", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 10 + ((LW - 20 - 8) / 3 + 4) * 2, y + 92, (LW - 20 - 8) / 3, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_START_MYTH, app::g_ctx.hInst, nullptr);

        // 右侧：高级工具
        const int btnW = (RW - 20 - 8) / 2;
        CreateWindowW(L"BUTTON", L"高级工具", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            R, y, RW, 140, hWnd, nullptr, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"解禁系统程序", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10, y + 22, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_RESTORE_SYS, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"重启资源管理器", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10 + btnW + 8, y + 22, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_RESTART_EXPLORER, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"解除网络限制", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10, y + 56, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_UNBLOCK_NET, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"解除U盘限制", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10 + btnW + 8, y + 56, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_UNBLOCK_USB, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"解除键盘锁", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10, y + 90, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_UNBLOCK_KEYBD, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"一键解除全部", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            R + 10 + btnW + 8, y + 90, btnW, 28, hWnd, (HMENU)(INT_PTR)IDC_BTN_UNBLOCK_ALL, app::g_ctx.hInst, nullptr);

        y = 162;
        // 窗口隐蔽
        CreateWindowW(L"BUTTON", L"窗口隐蔽", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            L, y, totalW, 105, hWnd, nullptr, app::g_ctx.hInst, nullptr);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            L + 10, y + 20, 200, 70, hWnd,
            (HMENU)IDC_LIST_HIDDEN, app::g_ctx.hInst, nullptr);

        const int sbW = 92;
        const int sbStartX = L + 220;
        const int sbGap = 8;
        CreateWindowW(L"BUTTON", L"隐藏当前", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 0 * (sbW + sbGap), y + 20, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_HIDE_CURRENT, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"恢复选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 1 * (sbW + sbGap), y + 20, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_RESTORE_SEL, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"恢复全部", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 2 * (sbW + sbGap), y + 20, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_RESTORE_ALL, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"选择模式", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 3 * (sbW + sbGap), y + 20, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_SELECT_MODE, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"截图预览", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 0 * (sbW + sbGap), y + 50, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_PREVIEW, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"悬浮窗", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 1 * (sbW + sbGap), y + 50, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_FLOAT, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"广播窗口化", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 2 * (sbW + sbGap), y + 50, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_BROADCAST_WIN, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"退出黑屏", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            sbStartX + 3 * (sbW + sbGap), y + 50, sbW, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_EXIT_BLACK, app::g_ctx.hInst, nullptr);

        y = 278;
        // 密码计算器
        CreateWindowW(L"BUTTON", L"动态密码计算器", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            L, y, totalW, 130, hWnd, nullptr, app::g_ctx.hInst, nullptr);

        CreateWindowW(L"STATIC", L"版本号:", WS_CHILD | WS_VISIBLE,
            L + 14, y + 22, 50, 20, hWnd, nullptr, app::g_ctx.hInst, nullptr);
        g_hVersion = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"11.06",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            L + 64, y + 20, 90, 22, hWnd, (HMENU)(INT_PTR)IDC_EDIT_VERSION, app::g_ctx.hInst, nullptr);

        CreateWindowW(L"STATIC", L"日期:", WS_CHILD | WS_VISIBLE,
            L + 164, y + 22, 34, 20, hWnd, nullptr, app::g_ctx.hInst, nullptr);
        {
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t dateBuf[32];
            swprintf(dateBuf, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            g_hDate = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", dateBuf,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                L + 198, y + 20, 100, 22, hWnd, (HMENU)(INT_PTR)IDC_EDIT_DATE, app::g_ctx.hInst, nullptr);
        }

        CreateWindowW(L"STATIC", L"计算机名:", WS_CHILD | WS_VISIBLE,
            L + 308, y + 22, 60, 20, hWnd, nullptr, app::g_ctx.hInst, nullptr);
        g_hPcName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pwcalc::GetLocalComputerName().c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            L + 368, y + 20, 140, 22, hWnd, (HMENU)(INT_PTR)IDC_EDIT_PCNAME, app::g_ctx.hInst, nullptr);

        CreateWindowW(L"BUTTON", L"计算密码", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 14, y + 50, 88, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_CALC_PASSWORD, app::g_ctx.hInst, nullptr);
        CreateWindowW(L"BUTTON", L"读取极域密码", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            L + 106, y + 50, 108, 26, hWnd, (HMENU)(INT_PTR)IDC_BTN_READ_MYTH_PWD, app::g_ctx.hInst, nullptr);

        g_hResult = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
            L + 222, y + 50, totalW - 232, 72, hWnd, (HMENU)(INT_PTR)IDC_EDIT_RESULT, app::g_ctx.hInst, nullptr);

        // 底部状态栏
        g_hStatus = CreateWindowW(STATUSCLASSNAMEW, L"等待操作",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)1005, app::g_ctx.hInst, nullptr);
        int pts[2] = {420, -1};
        SendMessageW(g_hStatus, SB_SETPARTS, 2, (LPARAM)pts);

        // 设置字体（状态栏需单独设置，EnumChildWindows 无法枚举到）
        if (g_hFont) {
            EnumChildWindows(hWnd, &mainwin::SetFontEnumProc, (LPARAM)g_hFont);
            SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        }

        SetTimer(hWnd, IDC_GUI_TIMER, 3000, nullptr);

        RefreshStatus();
        RefreshWindowList();
        AppendLog(L"图形界面已启动");
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBrush = GetSysColorBrush(COLOR_WINDOW);
        FillRect((HDC)wParam, &rc, hBrush);
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == IDC_GUI_TIMER) {
            RefreshStatus();
            RefreshWindowList();
        }
        return 0;

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);

        if (cmd == IDC_BTN_HIDE_CURRENT) {
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
            AppendLog(L"已进入选择模式");
        } else if (cmd == IDC_BTN_PREVIEW) {
            preview::Toggle();
        } else if (cmd == IDC_BTN_FLOAT) {
            floatw::Toggle();
        }

        else if (cmd == IDC_BTN_KILL_CLASSROOM) {
            auto r = mctl::KillClassroomHelper();
            AppendLog(L"杀机房助手: " + WSTR(r.killedCount) + L" 个进程");
        } else if (cmd == IDC_BTN_RESTORE_SYS) {
            auto r = mctl::UnblockSystemPrograms();
            AppendLog(L"解禁系统程序: " + r.detail);
        } else if (cmd == IDC_BTN_RESTART_EXPLORER) {
            if (MessageBoxW(hWnd, L"确认重启资源管理器？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                mctl::RestartExplorer();
                AppendLog(L"已重启资源管理器");
            }
        }

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
        } else if (cmd == IDC_BTN_EXIT_BLACK) {
            bool ok = pctl::ExitBlackScreen();
            AppendLog(ok ? L"已退出黑屏" : L"未找到黑屏窗口");
        }

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
            AppendLog(ok ? L"键盘锁已解除" : L"键盘锁解除失败");
        }

        else if (cmd == IDC_BTN_CALC_PASSWORD) {
            DoCalcPassword(hWnd);
        }
        else if (cmd == IDC_BTN_READ_MYTH_PWD) {
            std::wstring pwd = pwcalc::ReadMythwarePassword();
            if (pwd.empty()) {
                SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, L"未找到极域密码");
                AppendLog(L"读取极域密码: 未找到");
            } else {
                SetDlgItemTextW(hWnd, IDC_EDIT_RESULT, (L"极域注册表密码: " + pwd).c_str());
                AppendLog(L"读取极域密码: " + pwd);
            }
        }

        else if (cmd == IDC_BTN_REFRESH) {
            RefreshStatus();
            RefreshWindowList();
            AppendLog(L"已刷新状态");
        }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDC_GUI_TIMER);
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        g_hWnd = nullptr;
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = WIN_W;
        mmi->ptMinTrackSize.y = WIN_H;
        mmi->ptMaxTrackSize.x = WIN_W;
        mmi->ptMaxTrackSize.y = WIN_H;
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_GUI_CLASS;
    wc.hIcon         = LoadIcon(nullptr, IDI_SHIELD);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    return RegisterClassExW(&wc) != 0;
}

HWND Create(HINSTANCE hInst)
{
    g_hWnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        APP_GUI_CLASS,
        APP_TITLE,
        (WS_OVERLAPPEDWINDOW | WS_VISIBLE) ^ WS_MAXIMIZEBOX ^ WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        nullptr, nullptr, hInst, nullptr);
    return g_hWnd;
}

void Show()
{
    if (g_hWnd) {
        ShowWindow(g_hWnd, SW_SHOW);
        SetForegroundWindow(g_hWnd);
        SetWindowDisplayAffinity(g_hWnd, WDA_EXCLUDEFROMCAPTURE);
        RefreshStatus();
        RefreshWindowList();
        StartTopmostThread();
    }
}

void Hide()
{
    StopTopmostThread();
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

    auto st = pctl::GetMythwareStatus();
    std::wstring stateText;
    switch (st.state) {
    case pctl::MythwareState::NotRunning:  stateText = L"极域: 未运行"; break;
    case pctl::MythwareState::Running:     stateText = L"极域: 运行中"; break;
    case pctl::MythwareState::Suspended:   stateText = L"极域: 已挂起"; break;
    case pctl::MythwareState::NoResponse:  stateText = L"极域: 无响应"; break;
    }
    if (st.pid) stateText += L"  PID " + WSTR(st.pid);
    if (!st.version.empty()) stateText += L"  v" + st.version;

    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)stateText.c_str());

    std::wstring right = common::IsSelf64Bit() ? L"64位" : L"32位";
    std::wstring pwd = pwcalc::ReadMythwarePassword();
    if (!pwd.empty()) right += L"  |  密码: " + pwd;
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)right.c_str());
}

void RefreshWindowList()
{
    if (!g_hList) return;
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    const auto& hidden = whide::GetAll();
    for (const auto& hw : hidden) {
        std::wstring item = hw.title + L"  [" + hw.processName + L"]";
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

void AppendLog(const std::wstring& text)
{
    logger::Info(text);
    if (g_hStatus) {
        SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)text.c_str());
    }
}

} // namespace mainwin
