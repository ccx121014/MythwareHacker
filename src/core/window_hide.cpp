// window_hide.cpp - 窗口隐蔽实现（分级降级方案）
// ============================================================
// 降级链：穿透 → 黑块 → 移开
//   1. DWMWA_CLOAK（穿透）：DWM 完全停止渲染，截屏和视觉都穿透到后面内容
//   2. WDA_EXCLUDEFROMCAPTURE（黑块）：截屏时排除该窗口，区域填黑色，至少教师端看不到内容
//   3. 屏幕外移动（移开）：把窗口移到屏幕外，最可靠但本机也看不见
//
// 每一步只在上一步失败时才尝试，不混用（避免互相干扰）。
// ============================================================
#include "core/window_hide.h"
#include "core/inject.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace whide {

static std::vector<HiddenWindow> g_hidden;
static std::wstring g_lastDiag;

typedef BOOL (WINAPI *SetWindowDisplayAffinity_t)(HWND, DWORD);
static SetWindowDisplayAffinity_t GetSetWindowDisplayAffinity()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return nullptr;
    return reinterpret_cast<SetWindowDisplayAffinity_t>(
        GetProcAddress(hUser32, "SetWindowDisplayAffinity"));
}

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

// ============ 方案3：屏幕外移动 ============
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

// ============ 方案1：DWMWA_CLOAK（穿透）============
// 只做 Cloak，不混 WDA/Layered
static bool TryCloak(HWND hwnd, HiddenWindow& hw)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    bool isSelf = (pid == GetCurrentProcessId());

    if (isSelf) {
        // 本进程窗口：直接调
        auto pfnCloak = GetDwmSetWindowAttribute();
        if (!pfnCloak) return false;
        BOOL cloak = TRUE;
        HRESULT hr = pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        if (SUCCEEDED(hr)) {
            hw.cloakOk = true;
            return true;
        }
        return false;
    }

    // 跨进程：通过 DLL 注入调用 RemoteCloakWindow
    struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
    RemoteCloakParams p = { hwnd, TRUE };
    auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                   &p, sizeof(p));
    if (r.success && r.exitCode == 0) {
        hw.cloakOk = true;
        hw.injectOk = true;
        return true;
    }
    // InjectAndCall 失败可能是因为 DLL 没注入，尝试先 InjectDllOnly 再发 cloak 命令
    auto res = inject::InjectDllOnly(pid, L"MythwareHideHook");
    if (!res.success) return false;
    hw.injectOk = true;
    // 等待消息窗口
    for (int i = 0; i < 20; i++) {
        if (FindWindowW(nullptr, L"MythwareHacker MsgCenter Window")) break;
        Sleep(100);
    }
    // 再试一次远程调用
    auto r2 = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                    &p, sizeof(p));
    if (r2.success && r2.exitCode == 0) {
        hw.cloakOk = true;
        return true;
    }
    return false;
}

// ============ 方案2：WDA_EXCLUDEFROMCAPTURE（黑块）============
// 只做 WDA，不混 Cloak/Layered
static bool TryWda(HWND hwnd, HiddenWindow& hw)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    if (!common::IsWdaExcludeFromCaptureSupported()) return false;

    bool isSelf = (pid == GetCurrentProcessId());

    if (isSelf) {
        auto pfnWda = GetSetWindowDisplayAffinity();
        if (!pfnWda) return false;
        if (pfnWda(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
            hw.wdaOk = true;
            return true;
        }
        return false;
    }

    // 跨进程
    struct RemoteAffinityParams { HWND hwnd; DWORD affinity; };
    RemoteAffinityParams p = { hwnd, WDA_EXCLUDEFROMCAPTURE };
    auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                                   &p, sizeof(p));
    if (r.success && r.exitCode == 0) {
        hw.wdaOk = true;
        hw.injectOk = true;
        return true;
    }
    return false;
}

// ============ Hide：分级降级 ============
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
    hw.origPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &hw.origPlacement);

    bool ok = false;
    std::wstring method;

    // === 方案1：穿透（DWMWA_CLOAK）===
    if (TryCloak(hwnd, hw)) {
        ok = true;
        method = L"穿透(Cloak)";
    }

    // === 方案2：黑块（WDA_EXCLUDEFROMCAPTURE）===
    if (!ok) {
        if (TryWda(hwnd, hw)) {
            ok = true;
            method = L"黑块(WDA)";
        }
    }

    // === 方案3：移开（屏幕外）===
    if (!ok) {
        MoveOffscreen(hwnd, hw);
        ok = true;
        method = L"移开(屏幕外)";
        logger::Warn(L"Cloak和WDA均失败，降级到屏幕外: " + hw.title + L" 诊断: " + diagErr);
    }

    diagErr.clear();
    g_hidden.push_back(hw);
    SaveToPersist();

    logger::Info(L"隐蔽窗口: " + hw.title + L" [" + hw.processName + L"] 方案: " + method);
    return ok;
}

// ============ Restore：按记录的方案反向恢复 ============
bool Restore(HWND hwnd)
{
    auto it = std::find_if(g_hidden.begin(), g_hidden.end(),
        [hwnd](const HiddenWindow& hw) { return hw.hwnd == hwnd; });
    if (it == g_hidden.end()) return false;

    HiddenWindow hw = *it;
    g_hidden.erase(it);
    SaveToPersist();

    if (!IsWindow(hwnd)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    bool isSelf = (pid == GetCurrentProcessId());

    // 恢复 Cloak
    if (hw.cloakOk) {
        if (isSelf) {
            auto pfnCloak = GetDwmSetWindowAttribute();
            if (pfnCloak) {
                BOOL cloak = FALSE;
                pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            }
        } else {
            struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
            RemoteCloakParams p = { hwnd, FALSE };
            inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                  &p, sizeof(p));
        }
    }

    // 恢复 WDA
    if (hw.wdaOk) {
        if (isSelf) {
            auto pfnWda = GetSetWindowDisplayAffinity();
            if (pfnWda) pfnWda(hwnd, WDA_NONE);
        } else {
            struct RemoteAffinityParams { HWND hwnd; DWORD affinity; };
            RemoteAffinityParams p = { hwnd, WDA_NONE };
            inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                                  &p, sizeof(p));
        }
    }

    // 恢复屏幕外
    RestoreFromOffscreen(hwnd, hw);

    logger::Info(L"恢复窗口: " + hw.title);
    return true;
}

void RestoreAll()
{
    auto copy = g_hidden;
    for (const auto& hw : copy) {
        if (IsWindow(hw.hwnd)) Restore(hw.hwnd);
    }
    g_hidden.clear();
    persist::Delete();
}

void ToggleCurrent()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd)) {
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
