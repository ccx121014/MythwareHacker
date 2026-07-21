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
        // 使用系统消息字体，避免硬编码字体在某些系统上不可用导致乱码
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(NONCLIENTMETRICSW);
        HFONT hFont = nullptr;
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        }
        if (!hFont) {
            hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }
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
        // 不立即重绘，等下一次 timer 触发，避免调整窗口大小时卡顿
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

    HDC hScreenDC = GetDC(nullptr);
    if (!hScreenDC) return;

    // 直接从屏幕 DC 复制，性能远超逐窗口 PrintWindow
    SetStretchBltMode(g_hDCMem, HALFTONE);
    StretchBlt(g_hDCMem, 0, 0, g_width, g_height,
               hScreenDC, 0, 0, screenW, screenH, SRCCOPY);

    // 用黑色填充预览窗口自身区域，避免画中画递归效果
    RECT previewRc;
    if (GetWindowRect(app::g_ctx.hWndPreview, &previewRc)) {
        int fillX = (previewRc.left * g_width) / screenW;
        int fillY = (previewRc.top  * g_height) / screenH;
        int fillW = ((previewRc.right - previewRc.left) * g_width) / screenW;
        int fillH = ((previewRc.bottom - previewRc.top) * g_height) / screenH;
        RECT fillRc = { fillX, fillY, fillX + fillW, fillY + fillH };
        FillRect(g_hDCMem, &fillRc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    ReleaseDC(nullptr, hScreenDC);

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
