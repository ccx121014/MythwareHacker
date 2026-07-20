// main_window.cpp - 图形界面主窗口实现（美化版）
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
static HFONT g_hFontBold = nullptr;
static std::wstring g_logText;

// ---------------------------------------------------------------------------
// 颜色主题
// ---------------------------------------------------------------------------
#define C_BG        RGB(245, 247, 250)   // 窗口背景
#define C_BG_PANEL  RGB(255, 255, 255)   // 面板背景（白色）
#define C_TEXT      RGB(33, 37, 41)      // 主文字
#define C_TEXT_DIM  RGB(108, 117, 125)   // 次要文字
#define C_BORDER    RGB(222, 226, 230)   // 边框色

#define C_RED       RGB(220, 53, 69)     // 危险操作
#define C_RED_P     RGB(200, 35, 51)
#define C_GREEN     RGB(40, 167, 69)     // 恢复/安全
#define C_GREEN_P   RGB(33, 136, 56)
#define C_BLUE      RGB(0, 123, 255)     // 普通操作
#define C_BLUE_P    RGB(0, 105, 217)
#define C_ORANGE    RGB(253, 126, 20)    // 警告/解除限制
#define C_ORANGE_P  RGB(232, 112, 6)
#define C_PURPLE    RGB(111, 66, 192)    // 密码相关
#define C_PURPLE_P  RGB(96, 52, 175)
#define C_GRAY      RGB(108, 117, 125)   // 中性
#define C_GRAY_P    RGB(84, 91, 98)

struct BtnTheme {
    COLORREF bg;
    COLORREF bgPressed;
    COLORREF text;
};

static const BtnTheme THEME_RED    = { C_RED,    C_RED_P,    RGB(255,255,255) };
static const BtnTheme THEME_GREEN  = { C_GREEN,  C_GREEN_P,  RGB(255,255,255) };
static const BtnTheme THEME_BLUE   = { C_BLUE,   C_BLUE_P,   RGB(255,255,255) };
static const BtnTheme THEME_ORANGE = { C_ORANGE, C_ORANGE_P, RGB(255,255,255) };
static const BtnTheme THEME_PURPLE = { C_PURPLE, C_PURPLE_P, RGB(255,255,255) };
static const BtnTheme THEME_GRAY   = { C_GRAY,   C_GRAY_P,   RGB(255,255,255) };

static const BtnTheme* GetTheme(int id)
{
    switch (id) {
    case IDC_BTN_KILL_MYTH:
    case IDC_BTN_KILL_CLASSROOM:
        return &THEME_RED;
    case IDC_BTN_RESTORE_SEL:
    case IDC_BTN_RESTORE_ALL:
    case IDC_BTN_RESUME:
    case IDC_BTN_RESTORE_SYS:
    case IDC_BTN_RESTART_EXPLORER:
        return &THEME_GREEN;
    case IDC_BTN_HIDE_CURRENT:
    case IDC_BTN_SELECT_MODE:
    case IDC_BTN_PREVIEW:
    case IDC_BTN_FLOAT:
    case IDC_BTN_SUSPEND:
    case IDC_BTN_START_MYTH:
    case IDC_BTN_BROADCAST_WIN:
    case IDC_BTN_BROADCAST_FULL:
    case IDC_BTN_EXIT_BLACK:
    case IDC_BTN_REFRESH:
        return &THEME_BLUE;
    case IDC_BTN_UNBLOCK_NET:
    case IDC_BTN_UNBLOCK_USB:
    case IDC_BTN_UNBLOCK_ALL:
    case IDC_BTN_UNBLOCK_KEYBD:
        return &THEME_ORANGE;
    case IDC_BTN_CALC_PASSWORD:
    case IDC_BTN_READ_MYTH_PWD:
        return &THEME_PURPLE;
    default:
        return &THEME_GRAY;
    }
}

// ---------------------------------------------------------------------------
// 绘制辅助：超采样抗锯齿圆角（3x 渲染 → HALFTONE 缩放，纯 GDI 无依赖）
// ---------------------------------------------------------------------------

// 在目标 DC 上绘制抗锯齿圆角填充（radius=半圆最圆润）
static void FillRoundedRectAA(HDC hdc, int x, int y, int w, int h, COLORREF bg)
{
    if (w <= 0 || h <= 0) return;
    int radius = (h < w) ? h / 2 : w / 2;

    const int SS = 3;                       // 超采样倍数
    int sw = w * SS, sh = h * SS;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, sw, sh);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 用窗口背景色填充（圆角外区域将以此色与目标混合，实现平滑过渡）
    RECT fullRc = { 0, 0, sw, sh };
    HBRUSH hBgBrush = CreateSolidBrush(C_BG);
    FillRect(memDC, &fullRc, hBgBrush);
    DeleteObject(hBgBrush);

    // 绘制 3 倍大小的圆角矩形
    HBRUSH hBrush = CreateSolidBrush(bg);
    HGDIOBJ oldBrush = SelectObject(memDC, hBrush);
    HGDIOBJ oldPen = SelectObject(memDC, GetStockObject(NULL_PEN));
    RoundRect(memDC, 0, 0, sw, sh, radius * SS * 2, radius * SS * 2);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(hBrush);

    // HALFTONE 缩放回原尺寸，获得抗锯齿边缘
    int oldMode = SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, x, y, w, h, memDC, 0, 0, sw, sh, SRCCOPY);
    SetStretchBltMode(hdc, oldMode);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

static void DrawThemedButton(HDC hdc, RECT* rc, const wchar_t* text, UINT state, const BtnTheme* theme)
{
    COLORREF bgCR = (state & ODS_SELECTED) ? theme->bgPressed : theme->bg;
    if (state & ODS_DISABLED) bgCR = RGB(200, 200, 200);

    int x = rc->left, y = rc->top;
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    if (state & ODS_SELECTED) { x += 1; y += 1; w -= 2; h -= 2; }

    // 抗锯齿半圆角填充
    FillRoundedRectAA(hdc, x, y, w, h, bgCR);

    // GDI 绘制文字（保持原有字体）
    SetTextColor(hdc, (state & ODS_DISABLED) ? RGB(120,120,120) : theme->text);
    SetBkMode(hdc, TRANSPARENT);
    RECT textRc = { x, y, x + w, y + h };
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFont);
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOldFont);
}

// ---------------------------------------------------------------------------
// 布局常量
// ---------------------------------------------------------------------------
static const int MARGIN = 15;
static const int GAP = 12;
static const int COL_W = 326;
static const int BTN_H = 32;
static const int BTN_GAP = 6;

static const int WIN_W = 1040;
static const int WIN_H = 720;

// 列起始 X 坐标
#define COL1_X  MARGIN
#define COL2_X  (MARGIN + COL_W + GAP)
#define COL3_X  (MARGIN + COL_W*2 + GAP*2)

// ---------------------------------------------------------------------------
// 辅助：创建控件
// ---------------------------------------------------------------------------
static HWND MkBtn(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h)
{
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return btn;
}

static HWND MkLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool bold = false)
{
    HWND lbl = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, app::g_ctx.hInst, nullptr);
    if (bold && g_hFontBold)
        SendMessageW(lbl, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    else if (g_hFont)
        SendMessageW(lbl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
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
    if (g_hFontBold) SendMessageW(grp, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
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
    if (st.pid) s += L"  PID: " + WSTR(st.pid);
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

    auto all = pwcalc::CalculateAll(year, month, day, cn);
    std::wstring result = L"日期: " + WSTR(year) + L"-" +
                          WSTR(month) + L"-" + WSTR(day) +
                          L"  计算机: " + cn + L"\r\n" +
                          L"10.1前:     " + all.preV10 + L"\r\n" +
                          L"10.x:       " + all.v10ToV11 + L"\r\n" +
                          L"11.0x:      " + all.v11ToV1106 + L"\r\n" +
                          L"11.06~12.0: " + all.v1106ToV12 + L"\r\n";

    std::wstring pwd = pwcalc::CalculateAuto(verStr, year, month, day, cn);
    result += L"\r\n当前版本 " + verStr + L" → " + pwd;

    std::wstring mythPwd = pwcalc::ReadMythwarePassword();
    if (!mythPwd.empty()) {
        result += L"\r\n极域注册表密码: " + mythPwd;
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
        // 字体：常规 + 粗体
        g_hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        g_hFontBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        // ===== 顶部状态栏 =====
        g_hStatus = MkLabel(hWnd, L"极域状态: 检测中...", COL1_X, 10, 900, 26, true);
        MkBtn(hWnd, L"刷新", IDC_BTN_REFRESH, WIN_W - MARGIN - 80, 8, 80, 28);

        // ===== 左列 =====
        // 窗口隐蔽
        MkGroup(hWnd, L"窗口隐蔽", COL1_X, 46, COL_W, 310);
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            COL1_X + 10, 72, COL_W - 20, 200, hWnd,
            (HMENU)IDC_LIST_HIDDEN, app::g_ctx.hInst, nullptr);
        if (g_hFont) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        {
            int bx = COL1_X + 10;
            int by = 280;
            int bw = 100;
            MkBtn(hWnd, L"隐藏当前", IDC_BTN_HIDE_CURRENT, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复选中", IDC_BTN_RESTORE_SEL, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复全部", IDC_BTN_RESTORE_ALL, bx + (bw + BTN_GAP)*2, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"选择模式", IDC_BTN_SELECT_MODE, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"截图预览", IDC_BTN_PREVIEW, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"悬浮窗",   IDC_BTN_FLOAT, bx + (bw + BTN_GAP)*2, by, bw, BTN_H);
        }

        // 系统操作
        MkGroup(hWnd, L"系统操作", COL1_X, 370, COL_W, 108);
        {
            int bx = COL1_X + 10;
            int by = 396;
            int bw = 100;
            MkBtn(hWnd, L"杀机房助手",   IDC_BTN_KILL_CLASSROOM, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"解禁系统程序", IDC_BTN_RESTORE_SYS, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"重启资源管理器",IDC_BTN_RESTART_EXPLORER, bx + (bw + BTN_GAP)*2, by, bw, BTN_H);
        }

        // ===== 中列 =====
        // 极域控制
        MkGroup(hWnd, L"极域控制", COL2_X, 46, COL_W, 260);
        {
            int bx = COL2_X + 10;
            int by = 72;
            int bw = 100;
            MkBtn(hWnd, L"强杀极域",   IDC_BTN_KILL_MYTH,      bx, by, bw, BTN_H);
            MkBtn(hWnd, L"挂起极域",   IDC_BTN_SUSPEND,        bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复极域",   IDC_BTN_RESUME,         bx + (bw + BTN_GAP)*2, by, bw, BTN_H);
            by += BTN_H + 10;
            MkBtn(hWnd, L"启动极域",   IDC_BTN_START_MYTH,     bx, by, bw, BTN_H);
            MkBtn(hWnd, L"广播窗口化", IDC_BTN_BROADCAST_WIN,  bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"广播全屏化", IDC_BTN_BROADCAST_FULL, bx + (bw + BTN_GAP)*2, by, bw, BTN_H);
            by += BTN_H + 10;
            MkBtn(hWnd, L"退出黑屏",   IDC_BTN_EXIT_BLACK,     bx, by, bw, BTN_H);
        }

        // 限制解除
        MkGroup(hWnd, L"限制解除", COL2_X, 318, COL_W, 160);
        {
            int bx = COL2_X + 10;
            int by = 344;
            int bw = 153;
            MkBtn(hWnd, L"解除网络限制", IDC_BTN_UNBLOCK_NET,   bx, by, bw, BTN_H);
            MkBtn(hWnd, L"解除U盘限制",  IDC_BTN_UNBLOCK_USB,   bx + bw + BTN_GAP, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"一键解除全部", IDC_BTN_UNBLOCK_ALL,   bx, by, bw, BTN_H);
            MkBtn(hWnd, L"解除键盘锁",   IDC_BTN_UNBLOCK_KEYBD, bx + bw + BTN_GAP, by, bw, BTN_H);
        }

        // ===== 右列 =====
        // 密码计算器
        MkGroup(hWnd, L"动态密码计算器", COL3_X, 46, COL_W, 432);
        {
            int bx = COL3_X + 10;
            int by = 72;
            int lw = 65;   // 标签宽
            int ew = COL_W - lw - 24; // 输入框宽
            int lx = bx;
            int ex = bx + lw;

            MkLabel(hWnd, L"版本号:", lx, by + 3, lw, 22);
            MkEdit(hWnd, L"11.06", IDC_EDIT_VERSION, ex, by, ew, 26);
            by += 34;

            MkLabel(hWnd, L"日期:", lx, by + 3, lw, 22);
            {
                SYSTEMTIME st; GetLocalTime(&st);
                wchar_t dateBuf[32];
                swprintf(dateBuf, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
                MkEdit(hWnd, dateBuf, IDC_EDIT_DATE, ex, by, ew, 26);
            }
            by += 34;

            MkLabel(hWnd, L"计算机名:", lx, by + 3, lw, 22);
            MkEdit(hWnd, pwcalc::GetLocalComputerName().c_str(), IDC_EDIT_PCNAME, ex, by, ew, 26);
            by += 40;

            int bw2 = (COL_W - 24) / 2 - BTN_GAP/2;
            MkBtn(hWnd, L"计算密码", IDC_BTN_CALC_PASSWORD, bx, by, bw2, BTN_H);
            MkBtn(hWnd, L"读取极域密码", IDC_BTN_READ_MYTH_PWD, bx + bw2 + BTN_GAP, by, bw2, BTN_H);
            by += BTN_H + 14;

            MkLabel(hWnd, L"计算结果:", lx, by, lw + 50, 22);
            by += 22;
            MkEdit(hWnd, L"", IDC_EDIT_RESULT, bx, by, COL_W - 20, 200,
                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        }

        // ===== 底部操作日志 =====
        MkGroup(hWnd, L"操作日志", COL1_X, 490, WIN_W - MARGIN*2, 172);
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL,
            COL1_X + 10, 516, WIN_W - MARGIN*2 - 20, 136,
            hWnd, (HMENU)IDC_EDIT_LOG, app::g_ctx.hInst, nullptr);
        if (g_hFont) SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 定时刷新
        SetTimer(hWnd, IDC_GUI_TIMER, 3000, nullptr);

        RefreshStatus();
        RefreshWindowList();
        AppendLog(L"图形界面已启动");
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBrush = CreateSolidBrush(C_BG);
        FillRect((HDC)wParam, &rc, hBrush);
        DeleteObject(hBrush);
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType != ODT_BUTTON) break;
        wchar_t text[128] = {};
        GetWindowTextW(dis->hwndItem, text, 128);
        const BtnTheme* theme = GetTheme(dis->CtlID);
        DrawThemedButton(dis->hDC, &dis->rcItem, text, dis->itemState, theme);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, C_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, C_TEXT);
        static HBRUSH hEditBrush = nullptr;
        if (!hEditBrush) hEditBrush = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)hEditBrush;
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
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDC_GUI_TIMER);
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = nullptr; }
        g_hWnd = nullptr;
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = WIN_W;
        mmi->ptMinTrackSize.y = WIN_H;
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
        WS_EX_COMPOSITED,
        APP_GUI_CLASS,
        APP_TITLE L" - 集大成版",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
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
    if (g_logText.size() > 3000) {
        g_logText = g_logText.substr(0, 3000);
    }
    if (g_hLog) SetWindowTextW(g_hLog, g_logText.c_str());
    logger::Info(text);
}

} // namespace mainwin
