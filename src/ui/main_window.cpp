// main_window.cpp - 图形界面主窗口实现（JiYuTrainer 风格美化版）
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

namespace mainwin {

static HWND g_hWnd    = nullptr;
static HWND g_hList   = nullptr;
static HWND g_hStatus = nullptr;
static HFONT g_hFont  = nullptr;
static HFONT g_hFontBold = nullptr;
static HFONT g_hFontLarge = nullptr;
static HFONT g_hFontSmall = nullptr;
static std::wstring g_logText;

// ---------------------------------------------------------------------------
// 配色（参考 JiYuTrainer 蓝白渐变 + 状态色）
// ---------------------------------------------------------------------------
#define C_BG            RGB(240, 243, 247)   // 窗口浅灰蓝背景
#define C_CARD          RGB(255, 255, 255)   // 卡片白色
#define C_TEXT          RGB(33, 37, 41)
#define C_TEXT_LIGHT    RGB(255, 255, 255)
#define C_TEXT_DIM      RGB(108, 117, 125)
#define C_BORDER        RGB(222, 226, 230)

// 渐变顶部状态区
#define C_GRAD_TOP      RGB(46, 164, 240)    // 浅蓝
#define C_GRAD_BOTTOM   RGB(0, 123, 255)     // 深蓝

// 按钮主色（胶囊）
#define C_PRIMARY       RGB(0, 123, 255)
#define C_PRIMARY_P     RGB(0, 105, 217)
#define C_SUCCESS       RGB(40, 167, 69)
#define C_SUCCESS_P     RGB(33, 136, 56)
#define C_DANGER        RGB(220, 53, 69)
#define C_DANGER_P      RGB(200, 35, 51)
#define C_WARNING       RGB(255, 153, 0)
#define C_WARNING_P     RGB(228, 126, 0)
#define C_SECONDARY     RGB(108, 117, 125)
#define C_SECONDARY_P   RGB(84, 91, 98)

struct BtnTheme {
    COLORREF bg;
    COLORREF bgPressed;
    COLORREF text;
};

static const BtnTheme THEME_PRIMARY  = { C_PRIMARY,  C_PRIMARY_P,  RGB(255,255,255) };
static const BtnTheme THEME_SUCCESS  = { C_SUCCESS,  C_SUCCESS_P,  RGB(255,255,255) };
static const BtnTheme THEME_DANGER   = { C_DANGER,   C_DANGER_P,   RGB(255,255,255) };
static const BtnTheme THEME_WARNING  = { C_WARNING,  C_WARNING_P,  RGB(255,255,255) };
static const BtnTheme THEME_SECONDARY= { C_SECONDARY,C_SECONDARY_P,RGB(255,255,255) };

static const BtnTheme* GetTheme(int id)
{
    switch (id) {
    case IDC_BTN_KILL_MYTH:
    case IDC_BTN_KILL_CLASSROOM:
        return &THEME_DANGER;
    case IDC_BTN_RESTORE_SEL:
    case IDC_BTN_RESTORE_ALL:
    case IDC_BTN_RESUME:
    case IDC_BTN_RESTORE_SYS:
    case IDC_BTN_RESTART_EXPLORER:
        return &THEME_SUCCESS;
    case IDC_BTN_UNBLOCK_NET:
    case IDC_BTN_UNBLOCK_USB:
    case IDC_BTN_UNBLOCK_ALL:
    case IDC_BTN_UNBLOCK_KEYBD:
        return &THEME_WARNING;
    case IDC_BTN_CALC_PASSWORD:
    case IDC_BTN_READ_MYTH_PWD:
        return &THEME_PRIMARY;
    default:
        return &THEME_SECONDARY;
    }
}

// ---------------------------------------------------------------------------
// 尺寸与布局
// ---------------------------------------------------------------------------
static const int WIN_W = 900;
static const int WIN_H = 680;
static const int HEADER_H = 160;           // 顶部渐变状态区高度
static const int MARGIN = 20;
static const int CARD_GAP = 15;
static const int BTN_H = 34;               // 胶囊按钮高度
static const int BTN_GAP = 8;

// ---------------------------------------------------------------------------
// GDI 辅助
// ---------------------------------------------------------------------------
static void FillRoundedRectAA(HDC hdc, int x, int y, int w, int h, COLORREF bg, int radius)
{
    if (w <= 0 || h <= 0) return;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    const int SS = 3;
    int sw = w * SS, sh = h * SS;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, sw, sh);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 透明色填充（按钮后面可能是渐变，但 Win32 ownerdraw 不需要真透明，
    // 这里用背景色填充，绘制在卡片/窗口背景上即可）
    RECT fullRc = { 0, 0, sw, sh };
    HBRUSH hBgBrush = CreateSolidBrush(C_CARD);
    FillRect(memDC, &fullRc, hBgBrush);
    DeleteObject(hBgBrush);

    HBRUSH hBrush = CreateSolidBrush(bg);
    HGDIOBJ oldBrush = SelectObject(memDC, hBrush);
    HGDIOBJ oldPen = SelectObject(memDC, GetStockObject(NULL_PEN));
    RoundRect(memDC, 0, 0, sw, sh, radius * SS * 2, radius * SS * 2);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(hBrush);

    int oldMode = SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, x, y, w, h, memDC, 0, 0, sw, sh, SRCCOPY);
    SetStretchBltMode(hdc, oldMode);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

static void FillGradientRect(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom)
{
    TRIVERTEX vertices[2] = {};
    vertices[0].x = rc.left;
    vertices[0].y = rc.top;
    vertices[0].Red   = (GetRValue(top) << 8);
    vertices[0].Green = (GetGValue(top) << 8);
    vertices[0].Blue  = (GetBValue(top) << 8);
    vertices[0].Alpha = 0xFF00;

    vertices[1].x = rc.right;
    vertices[1].y = rc.bottom;
    vertices[1].Red   = (GetRValue(bottom) << 8);
    vertices[1].Green = (GetGValue(bottom) << 8);
    vertices[1].Blue  = (GetBValue(bottom) << 8);
    vertices[1].Alpha = 0xFF00;

    GRADIENT_RECT gRect = { 0, 1 };
    GradientFill(hdc, vertices, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
}

static void DrawShadowText(HDC hdc, const wchar_t* text, RECT& rc, COLORREF color, HFONT font)
{
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void DrawThemedButton(HDC hdc, RECT* rc, const wchar_t* text, UINT state, const BtnTheme* theme)
{
    COLORREF bgCR = (state & ODS_SELECTED) ? theme->bgPressed : theme->bg;
    if (state & ODS_DISABLED) bgCR = RGB(200, 200, 200);

    int x = rc->left, y = rc->top;
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    if (state & ODS_SELECTED) { x += 1; y += 1; w -= 2; h -= 2; }

    int radius = h / 2;  // 胶囊形
    FillRoundedRectAA(hdc, x, y, w, h, bgCR, radius);

    SetTextColor(hdc, (state & ODS_DISABLED) ? RGB(120,120,120) : theme->text);
    SetBkMode(hdc, TRANSPARENT);
    RECT textRc = { x, y, x + w, y + h };
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFont);
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOldFont);
}

// ---------------------------------------------------------------------------
// 控件创建辅助
// ---------------------------------------------------------------------------
static HWND MkBtn(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h)
{
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return btn;
}

static HWND MkLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr)
{
    HWND lbl = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, app::g_ctx.hInst, nullptr);
    HFONT f = font ? font : g_hFont;
    if (f) SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
    return lbl;
}

static HWND MkEdit(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h,
                   DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL)
{
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        style, x, y, w, h, parent, (HMENU)(INT_PTR)id, app::g_ctx.hInst, nullptr);
    if (g_hFont) SendMessageW(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return edit;
}

static HWND MkCard(HWND parent, int x, int y, int w, int h)
{
    // 卡片用一个简单的静态控件占位，背景在 WM_ERASEBKGND/WM_PAINT 里统一绘制
    HWND card = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, app::g_ctx.hInst, nullptr);
    return card;
}

// ---------------------------------------------------------------------------
// 状态格式化
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
    std::wstring s = stateText;
    if (st.pid) s += L"  ·  PID " + WSTR(st.pid);
    if (!st.version.empty()) s += L"  ·  v" + st.version;
    return s;
}

static std::wstring FormatMythwareSubStatus()
{
    std::wstring s = common::IsWin10Build19041OrLater() ? L"Win10 2004+" : L"兼容模式";
    s += L"  ·  ";
    s += common::IsSelf64Bit() ? L"64位" : L"32位";
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

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {

    case WM_CREATE: {
        // 字体
        g_hFont      = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        g_hFontBold  = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        g_hFontLarge = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        g_hFontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        int cx = MARGIN;
        int cy = HEADER_H + 20;
        int cw = (WIN_W - MARGIN * 2 - CARD_GAP) / 2;  // 两列卡片宽

        // ===== 左列：窗口隐蔽 =====
        MkCard(hWnd, cx, cy, cw, 260);
        MkLabel(hWnd, L"窗口隐蔽", cx + 15, cy + 12, 200, 24, g_hFontBold);
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            cx + 15, cy + 42, cw - 30, 130, hWnd,
            (HMENU)IDC_LIST_HIDDEN, app::g_ctx.hInst, nullptr);
        if (g_hFont) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        {
            int bx = cx + 15;
            int by = cy + 180;
            int bw = (cw - 30 - BTN_GAP * 2) / 3;
            MkBtn(hWnd, L"隐藏当前", IDC_BTN_HIDE_CURRENT, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复选中", IDC_BTN_RESTORE_SEL, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复全部", IDC_BTN_RESTORE_ALL, bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"选择模式", IDC_BTN_SELECT_MODE, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"截图预览", IDC_BTN_PREVIEW, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"悬浮窗",   IDC_BTN_FLOAT, bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
        }

        // ===== 右列上：极域控制 =====
        int rColX = cx + cw + CARD_GAP;
        MkCard(hWnd, rColX, cy, cw, 155);
        MkLabel(hWnd, L"极域控制", rColX + 15, cy + 12, 200, 24, g_hFontBold);
        {
            int bx = rColX + 15;
            int by = cy + 42;
            int bw = (cw - 30 - BTN_GAP * 2) / 3;
            MkBtn(hWnd, L"强杀极域",   IDC_BTN_KILL_MYTH,      bx, by, bw, BTN_H);
            MkBtn(hWnd, L"挂起极域",   IDC_BTN_SUSPEND,        bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"恢复极域",   IDC_BTN_RESUME,         bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"启动极域",   IDC_BTN_START_MYTH,     bx, by, bw, BTN_H);
            MkBtn(hWnd, L"广播窗口化", IDC_BTN_BROADCAST_WIN,  bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"广播全屏化", IDC_BTN_BROADCAST_FULL, bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"退出黑屏",   IDC_BTN_EXIT_BLACK,     bx, by, bw, BTN_H);
        }

        // ===== 右列下：限制解除 =====
        int rColY2 = cy + 155 + CARD_GAP;
        MkCard(hWnd, rColX, rColY2, cw, 155);
        MkLabel(hWnd, L"限制解除", rColX + 15, rColY2 + 12, 200, 24, g_hFontBold);
        {
            int bx = rColX + 15;
            int by = rColY2 + 42;
            int bw = (cw - 30 - BTN_GAP * 2) / 3;
            MkBtn(hWnd, L"解除网络", IDC_BTN_UNBLOCK_NET, bx, by, bw, BTN_H);
            MkBtn(hWnd, L"解除U盘",  IDC_BTN_UNBLOCK_USB, bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"解除键盘", IDC_BTN_UNBLOCK_KEYBD, bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
            by += BTN_H + BTN_GAP;
            MkBtn(hWnd, L"一键解除全部", IDC_BTN_UNBLOCK_ALL, bx, by, bw * 2 + BTN_GAP, BTN_H);
        }

        // ===== 中间系统操作行 =====
        int sysY = cy + 260 + CARD_GAP;
        MkCard(hWnd, cx, sysY, WIN_W - MARGIN * 2, 70);
        MkLabel(hWnd, L"系统操作", cx + 15, sysY + 12, 200, 24, g_hFontBold);
        {
            int bx = cx + 15;
            int by = sysY + 38;
            int bw = 130;
            MkBtn(hWnd, L"杀机房助手",    IDC_BTN_KILL_CLASSROOM,   bx, by, bw, BTN_H);
            MkBtn(hWnd, L"解禁系统程序",  IDC_BTN_RESTORE_SYS,      bx + bw + BTN_GAP, by, bw, BTN_H);
            MkBtn(hWnd, L"重启资源管理器",IDC_BTN_RESTART_EXPLORER, bx + (bw + BTN_GAP) * 2, by, bw, BTN_H);
            MkBtn(hWnd, L"刷新状态",      IDC_BTN_REFRESH,          bx + (bw + BTN_GAP) * 3, by, bw, BTN_H);
        }

        // ===== 底部密码计算 =====
        int pwdY = sysY + 70 + CARD_GAP;
        int pwdH = WIN_H - pwdY - MARGIN;
        MkCard(hWnd, cx, pwdY, WIN_W - MARGIN * 2, pwdH);
        MkLabel(hWnd, L"动态密码计算器", cx + 15, pwdY + 12, 200, 24, g_hFontBold);
        {
            int bx = cx + 15;
            int by = pwdY + 42;
            int lw = 70;
            int ew = 180;
            int inputGap = 200;

            MkLabel(hWnd, L"版本号:", bx, by + 4, lw, 22);
            MkEdit(hWnd, L"11.06", IDC_EDIT_VERSION, bx + lw, by, ew, 26);

            MkLabel(hWnd, L"日期:", bx + inputGap, by + 4, 45, 22);
            {
                SYSTEMTIME st; GetLocalTime(&st);
                wchar_t dateBuf[32];
                swprintf(dateBuf, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
                MkEdit(hWnd, dateBuf, IDC_EDIT_DATE, bx + inputGap + 45, by, 130, 26);
            }

            MkLabel(hWnd, L"计算机名:", bx + inputGap + 195, by + 4, 70, 22);
            MkEdit(hWnd, pwcalc::GetLocalComputerName().c_str(), IDC_EDIT_PCNAME,
                   bx + inputGap + 265, by, 180, 26);

            by += 40;
            MkBtn(hWnd, L"计算密码", IDC_BTN_CALC_PASSWORD, bx, by, 130, BTN_H);
            MkBtn(hWnd, L"读取极域密码", IDC_BTN_READ_MYTH_PWD, bx + 138, by, 130, BTN_H);

            int resultX = bx + 280;
            int resultW = WIN_W - MARGIN * 2 - 30 - 280;
            MkEdit(hWnd, L"", IDC_EDIT_RESULT, resultX, by - 40, resultW, pwdH - 60,
                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        }

        // ===== 日志在密码区内部右下角 =====
        // 简化：日志不单独放，直接输出到文件和托盘提示

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

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 绘制顶部渐变状态区
        RECT headerRc = { 0, 0, WIN_W, HEADER_H };
        FillGradientRect(hdc, headerRc, C_GRAD_TOP, C_GRAD_BOTTOM);

        // 大标题
        RECT titleRc = { MARGIN, 35, WIN_W - MARGIN, 75 };
        DrawShadowText(hdc, APP_TITLE, titleRc, RGB(255,255,255), g_hFontLarge);

        // 状态文字
        RECT statusRc = { MARGIN, 80, WIN_W - MARGIN, 110 };
        DrawShadowText(hdc, FormatMythwareStatus().c_str(), statusRc, RGB(255,255,255), g_hFontBold);

        RECT subRc = { MARGIN, 112, WIN_W - MARGIN, 135 };
        DrawShadowText(hdc, FormatMythwareSubStatus().c_str(), subRc, RGB(230,245,255), g_hFontSmall);

        // 绘制卡片背景（简单白色矩形，后续可加阴影）
        EndPaint(hWnd, &ps);
        return 0;
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
        } else if (cmd == IDC_BTN_BROADCAST_FULL) {
            bool ok = pctl::BroadcastToFullscreen();
            AppendLog(ok ? L"已将广播全屏化" : L"操作失败");
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
        if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = nullptr; }
        if (g_hFontLarge) { DeleteObject(g_hFontLarge); g_hFontLarge = nullptr; }
        if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = nullptr; }
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
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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
    if (!g_hStatus) {
        // 触发重绘顶部状态区
        if (g_hWnd) InvalidateRect(g_hWnd, nullptr, FALSE);
        return;
    }
}

void RefreshWindowList()
{
    if (!g_hList) return;
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    const auto& hidden = whide::GetAll();
    for (const auto& hw : hidden) {
        std::wstring item = hw.title + L"  [" + hw.processName + L"]";
        if (hw.wdaOk) item += L" WDA";
        else if (hw.injectOk) item += L" 注入";
        else if (hw.cloakOk) item += L" Cloak";
        else if (hw.offscreenOk) item += L" 屏幕外";
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

void AppendLog(const std::wstring& text)
{
    logger::Info(text);
}

} // namespace mainwin
