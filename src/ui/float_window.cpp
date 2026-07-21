// float_window.cpp - 圆形悬浮窗实现（带轮询置顶）
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
static HANDLE g_hTopmostThread = nullptr;
static bool g_topmostRunning = false;

// 轮询置顶线程（参考 MythwareToolkit）
static DWORD WINAPI TopmostThreadProc(LPVOID lpParameter)
{
    while (g_topmostRunning) {
        pctl::DemoteMythwareWindows();
        HWND hWnd = app::g_ctx.hWndFloat;
        if (hWnd && IsWindow(hWnd) && IsWindowVisible(hWnd)) {
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        Sleep(250);
    }
    return 0;
}

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
        pctl::BroadcastToWindowed();
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
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
        FillRect(hDC, &rc, hBrush);
        DeleteObject(hBrush);

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
        return 1;

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
        DWORD result = WaitForSingleObject(g_hTopmostThread, 300);
        if (result != WAIT_OBJECT_0) {
            TerminateThread(g_hTopmostThread, 0);
        }
        CloseHandle(g_hTopmostThread);
        g_hTopmostThread = nullptr;
    }
}

void Show()
{
    if (app::g_ctx.hWndFloat && IsWindow(app::g_ctx.hWndFloat)) {
        ShowWindow(app::g_ctx.hWndFloat, SW_SHOWNORMAL);
        SetWindowDisplayAffinity(app::g_ctx.hWndFloat, WDA_EXCLUDEFROMCAPTURE);
        StartTopmostThread();
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
        StartTopmostThread();
        logger::Info(L"悬浮窗已显示（轮询置顶）");
    }
}

void Hide()
{
    StopTopmostThread();
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
    StopTopmostThread();
    if (app::g_ctx.hWndFloat && IsWindow(app::g_ctx.hWndFloat)) {
        DestroyWindow(app::g_ctx.hWndFloat);
        app::g_ctx.hWndFloat = nullptr;
    }
}

} // namespace floatw