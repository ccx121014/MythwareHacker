// window_hide.cpp - 窗口隐蔽实现
#include "core/window_hide.h"
#include "core/inject.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace whide {

static std::vector<HiddenWindow> g_hidden;
static std::wstring g_lastDiag;

// 动态加载 SetWindowDisplayAffinity
typedef BOOL (WINAPI *SetWindowDisplayAffinity_t)(HWND, DWORD);
static SetWindowDisplayAffinity_t GetSetWindowDisplayAffinity()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return nullptr;
    return reinterpret_cast<SetWindowDisplayAffinity_t>(
        GetProcAddress(hUser32, "SetWindowDisplayAffinity"));
}

// 动态加载 DwmSetWindowAttribute
typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
static DwmSetWindowAttribute_t GetDwmSetWindowAttribute()
{
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return nullptr;
    return reinterpret_cast<DwmSetWindowAttribute_t>(
        GetProcAddress(hDwm, "DwmSetWindowAttribute"));
}

const std::vector<HiddenWindow>& GetAll() { return g_hidden; }

bool IsHidden(HWND hwnd)
{
    for (const auto& hw : g_hidden)
        if (hw.hwnd == hwnd) return true;
    return false;
}

void Cleanup()
{
    auto it = g_hidden.begin();
    while (it != g_hidden.end()) {
        if (!IsWindow(it->hwnd)) {
            it = g_hidden.erase(it);
        } else {
            it->title = wutil::GetWindowTitle(it->hwnd);
            ++it;
        }
    }
}

// 方案4：移到屏幕外
static void MoveOffscreen(HWND hwnd, HiddenWindow& hw)
{
    hw.origPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &hw.origPlacement);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    WINDOWPLACEMENT wp = hw.origPlacement;
    LONG w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    LONG h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    wp.rcNormalPosition.left   = screenW + 100;
    wp.rcNormalPosition.top    = screenH + 100;
    wp.rcNormalPosition.right  = screenW + 100 + w;
    wp.rcNormalPosition.bottom = screenH + 100 + h;
    wp.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(hwnd, &wp);
    hw.offscreenOk = true;
}

static void RestoreFromOffscreen(HWND hwnd, const HiddenWindow& hw)
{
    if (!hw.offscreenOk || !IsWindow(hwnd)) return;
    WINDOWPLACEMENT wp = hw.origPlacement;
    wp.length = sizeof(WINDOWPLACEMENT);
    SetWindowPlacement(hwnd, &wp);
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
}

bool Hide(HWND hwnd, std::wstring& diagErr)
{
    diagErr.clear();

    if (!wutil::IsWindowEligible(hwnd)) {
        diagErr = L"窗口不可用";
        return false;
    }

    if (IsHidden(hwnd)) {
        diagErr = L"窗口已隐蔽";
        return false;
    }

    HiddenWindow hw;
    hw.hwnd = hwnd;
    hw.title = wutil::GetWindowTitle(hwnd);
    hw.processName = wutil::GetProcessName(hwnd);

    bool anySuccess = false;

    // 方案1：本进程调用 WDA（对其他进程窗口通常无效，但尝试）
    auto pfnWda = GetSetWindowDisplayAffinity();
    if (pfnWda && common::IsWdaExcludeFromCaptureSupported()) {
        hw.wdaOk = pfnWda(hwnd, WDA_EXCLUDEFROMCAPTURE);
        if (hw.wdaOk) anySuccess = true;
    }

    // 方案2：DLL 注入跨进程调用 WDA
    if (!hw.wdaOk) {
        struct RemoteParams { HWND hwnd; DWORD affinity; };
        RemoteParams params = { hwnd, WDA_EXCLUDEFROMCAPTURE };
        auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                                       &params, sizeof(params));
        if (r.success && r.exitCode == 0) {
            hw.injectOk = true;
            anySuccess = true;
        } else if (!r.success) {
            diagErr = r.error;
        } else {
            diagErr = L"远程调用失败，退出码: " + std::to_wstring(r.exitCode);
        }
    }

    // 方案3：DWM Cloak（Win8+，本进程）
    if (!anySuccess) {
        auto pfnCloak = GetDwmSetWindowAttribute();
        if (pfnCloak) {
            BOOL cloak = TRUE;
            HRESULT hr = pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            hw.cloakOk = SUCCEEDED(hr);
            if (hw.cloakOk) anySuccess = true;
            if (anySuccess) diagErr.clear();
        }
    }

    // 方案4：屏幕外移动（兜底）
    if (!anySuccess) {
        MoveOffscreen(hwnd, hw);
        anySuccess = true;
        diagErr.clear();
        logger::Warn(L"WDA/注入/Cloak 均失败，使用屏幕外方案: " + hw.title + L" 诊断: " + diagErr);
    }

    g_hidden.push_back(hw);
    SaveToPersist();

    logger::Info(L"隐蔽窗口: " + hw.title + L" [" + hw.processName + L"]" +
              L" 方案: " + std::wstring(hw.wdaOk ? L"WDA " : L"") +
              std::wstring(hw.injectOk ? L"注入 " : L"") +
              std::wstring(hw.cloakOk ? L"Cloak " : L"") +
              std::wstring(hw.offscreenOk ? L"屏幕外" : L""));

    return anySuccess;
}

bool Restore(HWND hwnd)
{
    auto it = std::find_if(g_hidden.begin(), g_hidden.end(),
        [hwnd](const HiddenWindow& hw) { return hw.hwnd == hwnd; });
    if (it == g_hidden.end()) return false;

    HiddenWindow hw = *it;
    g_hidden.erase(it);
    SaveToPersist();

    if (!IsWindow(hwnd)) return false;

    // 反向执行所有可能生效的方案
    DWORD hwndPid = 0;
    GetWindowThreadProcessId(hwnd, &hwndPid);

    auto pfnWda = GetSetWindowDisplayAffinity();
    if (pfnWda && hwndPid == GetCurrentProcessId()) {
        pfnWda(hwnd, WDA_NONE);
    }

    // 注入恢复（affinity=0）
    struct RemoteParams { HWND hwnd; DWORD affinity; };
    RemoteParams params = { hwnd, WDA_NONE };
    inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                          &params, sizeof(params));

    auto pfnCloak = GetDwmSetWindowAttribute();
    if (pfnCloak) {
        BOOL cloak = FALSE;
        pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }

    RestoreFromOffscreen(hwnd, hw);

    logger::Info(L"恢复窗口: " + hw.title);
    return true;
}

void RestoreAll()
{
    auto copy = g_hidden;
    for (const auto& hw : copy) {
        if (IsWindow(hw.hwnd)) {
            Restore(hw.hwnd);
        }
    }
    g_hidden.clear();
    persist::Delete();
}

void ToggleCurrent()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd)) {
        // 前台无效，找第一个可用窗口
        hwnd = GetWindow(GetDesktopWindow(), GW_CHILD);
        while (hwnd) {
            if (IsWindowVisible(hwnd) && wutil::IsWindowEligible(hwnd))
                break;
            hwnd = GetWindow(hwnd, GW_HWNDNEXT);
        }
    }
    if (!hwnd) return;

    std::wstring diag;
    if (IsHidden(hwnd)) {
        Restore(hwnd);
    } else {
        Hide(hwnd, diag);
    }
}

void SaveToPersist()
{
    std::vector<persist::SavedWindow> saved;
    for (const auto& hw : g_hidden) {
        persist::SavedWindow sw;
        sw.placement = hw.origPlacement;
        sw.title = hw.title;
        sw.processName = hw.processName;
        saved.push_back(sw);
    }
    persist::Save(saved);
}

} // namespace whide
