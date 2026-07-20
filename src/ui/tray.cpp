// tray.cpp - 系统托盘实现
#include "ui/tray.h"
#include "ui/app_state.h"
#include "ui/menu.h"
#include "core/window_hide.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace tray {

static void SafeWcsCopy(WCHAR* dest, size_t destSize, const wchar_t* src)
{
    size_t srcLen = wcslen(src);
    if (srcLen >= destSize) srcLen = destSize - 1;
    wcsncpy(dest, src, srcLen);
    dest[srcLen] = L'\0';
}

void Add(HWND hWnd)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(NOTIFYICONDATAW);
    nid.hWnd             = hWnd;
    nid.uID              = ID_TRAY_ICON;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIcon(nullptr, IDI_SHIELD);
    SafeWcsCopy(nid.szTip, 128, APP_TITLE L" - 初始化中…");
    Shell_NotifyIconW(NIM_ADD, &nid);
    UpdateTip(hWnd);
}

void Remove(HWND hWnd)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = hWnd;
    nid.uID    = ID_TRAY_ICON;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void UpdateTip(HWND hWnd)
{
    whide::Cleanup();

    auto allWindows = wutil::EnumAllTopLevelWindows();
    for (auto& w : allWindows) {
        w.hidden = whide::IsHidden(w.hwnd);
    }

    std::wstring tip = APP_TITLE;

    int shownCount = 0, hiddenCount = 0;
    for (const auto& w : allWindows) {
        if (w.hidden) hiddenCount++;
        else shownCount++;
    }

    tip += L" [" + WSTR(shownCount) + L"可见";
    if (hiddenCount > 0)
        tip += L", " + WSTR(hiddenCount) + L"隐蔽";
    tip += L"]";

    for (const auto& w : allWindows) {
        std::wstring line = L"\n";
        if (w.hidden) line += L"[隐] ";
        std::wstring shortTitle = w.title;
        if (shortTitle.length() > 12)
            shortTitle = shortTitle.substr(0, 11) + L"…";
        line += shortTitle;
        if (tip.length() + line.length() > 124) {
            tip += L"\n…";
            break;
        }
        tip += line;
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = hWnd;
    nid.uID    = ID_TRAY_ICON;
    nid.uFlags = NIF_TIP;
    SafeWcsCopy(nid.szTip, 128, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowContextMenu(HWND hWnd)
{
    menu::ShowTrayMenu(hWnd);
}

} // namespace tray
