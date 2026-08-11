// float_window.cpp - 圆形悬浮窗实现（状态实时刷新）
#include "ui/float_window.h"
#include "ui/app_state.h"
#include "ui/menu.h"
#include "core/process_control.h"
#include "core/window_hide.h"
#include "utils/log.h"

namespace floatw {

static const int FLOAT_SIZE = 48;  // 圆窗直径
static const UINT_PTR TIMER_ID = 1;
static const UINT TIMER_INTERVAL = 1500;  // 1.5秒刷新一次
static bool g_dragging = false;
static POINT g_dragStart = {};

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        // 启用分层窗口（圆形 + 半透明）
        HRGN hRgn = CreateEllipticRgn(0, 0, FLOAT_SIZE, FLOAT_SIZE);
        SetWindowRgn(hWnd, hRgn, TRUE);
        SetWindowLong(hWnd, GWL_EXSTYLE,
                      GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        typedef BOOL (WINAPI *SetLayeredWindowAttributes_t)(HWND, COLORREF, BYTE, DWORD);
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        auto pfn = (SetLayeredWindowAttributes_t)GetProcAddress(hUser32, "SetLayeredWindowAttributes");
        if (pfn) pfn(hWnd, 0, 220, LWA_ALPHA);
        // 启动定时器定期刷新状态
        SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, nullptr);
        return 0;
    }

    case WM_TIMER: {
        // 定时刷新显示
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
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
        POINT now;
        GetCursorPos(&now);
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int dx = abs(now.x - rc.left - g_dragStart.x);
        int dy = abs(now.y - rc.top - g_dragStart.y);
        if (dx < 5 && dy < 5) {
            menu::ShowTrayMenu(app::g_ctx.hWndMain);
        }
        return 0;
    }

    case WM_MBUTTONDOWN: {
        // 中键：切换广播全屏/窗口化
        pctl::ToggleBroadcastMode();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_RBUTTONUP: {
        menu::ShowTrayMenu(app::g_ctx.hWndMain);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);

        // === 根据极域状态选择背景色 ===
        auto status = pctl::GetMythwareStatus();
        COLORREF bgColor;
        std::wstring text;

        if (status.state == pctl::MythwareState::NotRunning) {
            bgColor = RGB(100, 100, 100);  // 灰色：未运行
            text = L"·";
        } else if (status.state == pctl::MythwareState::Suspended) {
            bgColor = RGB(255, 140, 0);    // 橙色：已挂起
            text = L"M";
        } else {
            // 运行中：根据广播模式显示
            bgColor = RGB(0, 120, 215);    // 蓝色：正常运行
            if (pctl::IsBroadcastFullscreen()) {
                text = L"F";               // 全屏广播
            } else {
                // 检查是否有广播窗口
                // IsBroadcastFullscreen 返回false 可能是无广播 或 窗口化
                // 用 FindBroadcastWindow 间接判断（通过尝试窗口化结果）
                // 这里简单处理：显示 W 表示窗口化/无广播
                text = L"W";
            }
        }

        // === 画背景圆 ===
        HBRUSH hBrush = CreateSolidBrush(bgColor);
        FillRect(hDC, &rc, hBrush);
        DeleteObject(hBrush);

        // === 置顶指示：金色外圈 ===
        if (pctl::IsBroadcastTopmost()) {
            HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 215, 0));
            HPEN hOldPen = (HPEN)SelectObject(hDC, hPen);
            HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hDC, hNullBrush);
            Ellipse(hDC, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(hDC, hOldPen);
            SelectObject(hDC, hOldBrush);
            DeleteObject(hPen);
        }

        // === 画文字 ===
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 255, 255));
        HFONT hFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Arial");
        HFONT hOld = (HFONT)SelectObject(hDC, hFont);
        DrawTextW(hDC, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hDC, hOld);
        DeleteObject(hFont);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY: {
        KillTimer(hWnd, TIMER_ID);
        return 0;
    }

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
        SetWindowPos(app::g_ctx.hWndFloat, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowDisplayAffinity(app::g_ctx.hWndFloat, WDA_EXCLUDEFROMCAPTURE);
        return;
    }

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
        SetWindowDisplayAffinity(app::g_ctx.hWndFloat, WDA_EXCLUDEFROMCAPTURE);
        logger::Info(L"悬浮窗已显示（事件驱动置顶）");
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