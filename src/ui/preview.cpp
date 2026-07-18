// preview.cpp - 截图预览窗口实现
#include "ui/preview.h"
#include "ui/app_state.h"
#include "core/window_hide.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace preview {

static HBITMAP g_hBitmap = nullptr;
static HDC     g_hDCMem  = nullptr;
static int     g_width   = 0;
static int     g_height  = 0;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_TIMER:
        if (wParam == PREVIEW_TIMER_ID) {
            UpdateBitmap();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;
        int drawH = clientH - 30;
        if (drawH < 10) drawH = clientH;

        if (g_hDCMem && g_hBitmap) {
            BitBlt(hDC, 0, 0, g_width, g_height, g_hDCMem, 0, 0, SRCCOPY);
        } else {
            FillRect(hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }

        RECT tipRc = { 0, drawH, clientW, clientH };
        FillRect(hDC, &tipRc, (HBRUSH)GetStockObject(GRAY_BRUSH));

        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 0, 0));
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);

        std::wstring tip = L"提示：此为 GDI 截图预览。按 Win+Shift+S 用系统截图工具可准确验证 WDA 效果。";
        DrawTextW(hDC, tip.c_str(), (int)tip.length(), &tipRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_WORD_ELLIPSIS);

        SelectObject(hDC, hOldFont);
        DeleteObject(hFont);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
        if (g_hDCMem)   { DeleteDC(g_hDCMem);     g_hDCMem = nullptr; }
        g_width = 0;
        g_height = 0;
        UpdateBitmap();
        return 0;

    case WM_CLOSE:
        KillTimer(hWnd, PREVIEW_TIMER_ID);
        app::g_ctx.hWndPreview = nullptr;
        DestroyWindow(hWnd);
        return 0;

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
    wc.lpszClassName = APP_PREVIEW_CLASS;
    wc.hIcon         = LoadIcon(nullptr, IDI_INFORMATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    return RegisterClassExW(&wc);
}

void UpdateBitmap()
{
    if (!app::g_ctx.hWndPreview || !IsWindow(app::g_ctx.hWndPreview)) return;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    RECT rc;
    GetClientRect(app::g_ctx.hWndPreview, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;
    int drawH = clientH - 30;
    if (drawH < 10) drawH = clientH;

    if (clientW != g_width || drawH != g_height) {
        if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
        if (g_hDCMem)   { DeleteDC(g_hDCMem);     g_hDCMem = nullptr; }
        g_width = clientW;
        g_height = drawH;
    }

    if (!g_hDCMem) {
        HDC hScreenDC = GetDC(nullptr);
        if (!hScreenDC) return;
        g_hDCMem = CreateCompatibleDC(hScreenDC);
        g_hBitmap = CreateCompatibleBitmap(hScreenDC, g_width, g_height);
        ReleaseDC(nullptr, hScreenDC);
        if (!g_hDCMem || !g_hBitmap) return;
        SelectObject(g_hDCMem, g_hBitmap);
    }

    RECT rcFill = { 0, 0, g_width, g_height };
    FillRect(g_hDCMem, &rcFill, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 收集所有可见窗口（排除预览窗口自身）
    std::vector<HWND> hwndOrder;
    HWND h = GetWindow(GetDesktopWindow(), GW_CHILD);
    while (h) {
        if (h != app::g_ctx.hWndPreview && h != app::g_ctx.hWndFloat &&
            IsWindowVisible(h)) {
            hwndOrder.push_back(h);
        }
        h = GetWindow(h, GW_HWNDNEXT);
    }
    // GW_HWNDNEXT 顶层到底层，反转为底层到顶层
    std::reverse(hwndOrder.begin(), hwndOrder.end());

    for (HWND hw : hwndOrder) {
        if (!IsWindowVisible(hw)) continue;

        RECT wrc;
        if (!GetWindowRect(hw, &wrc)) continue;

        int winW = wrc.right - wrc.left;
        int winH = wrc.bottom - wrc.top;
        if (winW <= 0 || winH <= 0) continue;

        HDC hWinDC = GetWindowDC(hw);
        if (!hWinDC) continue;
        HDC hMemDC = CreateCompatibleDC(hWinDC);
        if (!hMemDC) { ReleaseDC(hw, hWinDC); continue; }
        HBITMAP hBmp = CreateCompatibleBitmap(hWinDC, winW, winH);
        if (!hBmp) { DeleteDC(hMemDC); ReleaseDC(hw, hWinDC); continue; }
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);

        BOOL ok = PrintWindow(hw, hMemDC, PW_RENDERFULLCONTENT);
        if (!ok) ok = PrintWindow(hw, hMemDC, 0);

        int dstX = (wrc.left * g_width) / screenW;
        int dstY = (wrc.top  * g_height) / screenH;
        int dstW = (winW     * g_width) / screenW;
        int dstH = (winH     * g_height) / screenH;

        if (dstX < 0) dstX = 0;
        if (dstY < 0) dstY = 0;
        if (dstX + dstW > g_width)  dstW = g_width - dstX;
        if (dstY + dstH > g_height) dstH = g_height - dstY;

        SetStretchBltMode(g_hDCMem, HALFTONE);
        StretchBlt(g_hDCMem, dstX, dstY, dstW, dstH,
                   hMemDC, 0, 0, winW, winH, SRCCOPY);

        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(hw, hWinDC);
    }

    InvalidateRect(app::g_ctx.hWndPreview, nullptr, FALSE);
}

void Toggle()
{
    if (app::g_ctx.hWndPreview && IsWindow(app::g_ctx.hWndPreview)) {
        KillTimer(app::g_ctx.hWndPreview, PREVIEW_TIMER_ID);
        DestroyWindow(app::g_ctx.hWndPreview);
        app::g_ctx.hWndPreview = nullptr;
        return;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int w = (int)(screenW * 0.6);
    int h = (int)(screenH * 0.6);
    if (w < 640) w = 640;
    if (h < 480) h = 480;

    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    app::g_ctx.hWndPreview = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        APP_PREVIEW_CLASS,
        L"截图预览 - 模拟教师端视角（按 Ctrl+Shift+P 关闭）",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        x, y, w, h,
        nullptr, nullptr, app::g_ctx.hInst, nullptr);

    if (app::g_ctx.hWndPreview) {
        ShowWindow(app::g_ctx.hWndPreview, SW_SHOW);
        UpdateWindow(app::g_ctx.hWndPreview);
        UpdateBitmap();
        SetTimer(app::g_ctx.hWndPreview, PREVIEW_TIMER_ID, PREVIEW_INTERVAL_MS, nullptr);
    }
}

void Cleanup()
{
    if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
    if (g_hDCMem)   { DeleteDC(g_hDCMem);     g_hDCMem = nullptr; }
    if (app::g_ctx.hWndPreview && IsWindow(app::g_ctx.hWndPreview)) {
        DestroyWindow(app::g_ctx.hWndPreview);
        app::g_ctx.hWndPreview = nullptr;
    }
}

} // namespace preview
