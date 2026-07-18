// float_window.cpp - 圆形悬浮窗实现
#include "ui/float_window.h"
#include "ui/app_state.h"
#include "ui/menu.h"
#include "core/process_control.h"
#include "core/window_hide.h"
#include "utils/log.h"

namespace floatw {

static const int FLOAT_SIZE = 48;  // 圆窗直径
static bool g_dragging = false;
static POINT g_dragStart = {};

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        // 启用分层窗口（圆形 + 半透明）
        // 实际圆形通过 region 实现
        HRGN hRgn = CreateEllipticRgn(0, 0, FLOAT_SIZE, FLOAT_SIZE);
        SetWindowRgn(hWnd, hRgn, TRUE);
        // 设置分层透明度
        SetWindowLong(hWnd, GWL_EXSTYLE,
                      GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        // 透明度 220（0-255）
        typedef BOOL (WINAPI *SetLayeredWindowAttributes_t)(HWND, COLORREF, BYTE, DWORD);
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        auto pfn = (SetLayeredWindowAttributes_t)GetProcAddress(hUser32, "SetLayeredWindowAttributes");
        if (pfn) pfn(hWnd, 0, 220, LWA_ALPHA);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        // 左键：弹出主菜单（切换主面板）
        // 同时支持拖拽
        g_dragging = true;
        g_dragStart = { LOWORD(lParam), HIWORD(lParam) };
        SetCapture(hWnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_dragging) {
            POINT pt;
            GetCursorPos(&pt);
            RECT rc;
            GetWindowRect(hWnd, &rc);
            int dx = pt.x - (rc.left + g_dragStart.x);
            int dy = pt.y - (rc.top + g_dragStart.y);
            SetWindowPos(hWnd, nullptr, rc.left + dx, rc.top + dy,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        bool wasDragging = g_dragging;
        g_dragging = false;
        ReleaseCapture();
        // 如果几乎没移动，视为点击
        POINT now;
        GetCursorPos(&now);
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int dx = abs(now.x - rc.left - g_dragStart.x);
        int dy = abs(now.y - rc.top - g_dragStart.y);
        if (dx < 5 && dy < 5) {
            // 点击：弹出托盘菜单
            menu::ShowTrayMenu(app::g_ctx.hWndMain);
        }
        return 0;
    }

    case WM_MBUTTONDOWN: {
        // 中键：一键广播窗口化
        pctl::BroadcastToWindowed();
        return 0;
    }

    case WM_RBUTTONUP: {
        // 右键：快捷菜单
        menu::ShowTrayMenu(app::g_ctx.hWndMain);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(hWnd, &ps);

        // 画圆形背景（盾牌色：深蓝渐变简化为纯色）
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
        FillRect(hDC, &rc, hBrush);
        DeleteObject(hBrush);

        // 画 "M" 字母（MythwareHacker）
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 255, 255));
        HFONT hFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Arial");
        HFONT hOld = (HFONT)SelectObject(hDC, hFont);
        std::wstring text = L"M";
        DrawTextW(hDC, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hDC, hOld);
        DeleteObject(hFont);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // 防止闪烁

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

ATOM RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_FLOAT_CLASS;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    return RegisterClassExW(&wc);
}

void Show()
{
    if (app::g_ctx.hWndFloat && IsWindow(app::g_ctx.hWndFloat)) {
        ShowWindow(app::g_ctx.hWndFloat, SW_SHOWNORMAL);
        return;
    }

    // 默认位置：屏幕右上角
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int x = screenW - FLOAT_SIZE - 20;
    int y = 20;

    app::g_ctx.hWndFloat = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        APP_FLOAT_CLASS,
        L"",
        WS_POPUP,
        x, y, FLOAT_SIZE, FLOAT_SIZE,
        nullptr, nullptr, app::g_ctx.hInst, nullptr);

    if (app::g_ctx.hWndFloat) {
        ShowWindow(app::g_ctx.hWndFloat, SW_SHOWNORMAL);
        UpdateWindow(app::g_ctx.hWndFloat);
        logger::Info(L"悬浮窗已显示");
    }
}

void Hide()
{
    if (app::g_ctx.hWndFloat && IsWindow(app::g_ctx.hWndFloat)) {
        ShowWindow(app::g_ctx.hWndFloat, SW_HIDE);
    }
}

void Toggle()
{
    if (IsVisible()) {
        Hide();
    } else {
        Show();
    }
}

bool IsVisible()
{
    return app::g_ctx.hWndFloat && IsWindowVisible(app::g_ctx.hWndFloat);
}

void Cleanup()
{
    if (app::g_ctx.hWndFloat && IsWindow(app::g_ctx.hWndFloat)) {
        DestroyWindow(app::g_ctx.hWndFloat);
        app::g_ctx.hWndFloat = nullptr;
    }
}

} // namespace floatw
