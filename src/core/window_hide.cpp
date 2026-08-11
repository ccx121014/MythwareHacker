// window_hide.cpp - 窗口隐蔽实现（DWMWA_CLOAK 优先方案）
// ============================================================
// 核心改进：解决"WDA_EXCLUDEFROMCAPTURE 导致截屏黑块"的问题
//
// 旧方案：WDA_EXCLUDEFROMCAPTURE → DWM 截屏时跳过该区域，填黑色 → 黑块
// 新方案：DWMWA_CLOAK + WDA + WS_EX_LAYERED 三层组合 → 窗口完全消失 + 穿透
// ============================================================
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

// 方案：移到屏幕外（最终兜底）
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

// 方案：直接在当前进程对窗口做 Cloak + WDA + Layered（本进程窗口用）
static bool ApplyCloakSameProcess(HWND hwnd, HiddenWindow& hw)
{
    bool any = false;
    // DWM Cloak
    auto pfnCloak = GetDwmSetWindowAttribute();
    if (pfnCloak) {
        BOOL cloak = TRUE;
        HRESULT hr = pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        if (SUCCEEDED(hr)) {
            hw.cloakOk = true;
            any = true;
        }
    }
    // WDA 辅助
    auto pfnWda = GetSetWindowDisplayAffinity();
    if (pfnWda && common::IsWdaExcludeFromCaptureSupported()) {
        hw.wdaOk = pfnWda(hwnd, WDA_EXCLUDEFROMCAPTURE);
        if (hw.wdaOk) any = true;
    }
    // Layered alpha=0 兜底
    typedef BOOL (WINAPI *SetLayeredWindowAttributes_t)(HWND, COLORREF, BYTE, DWORD);
    auto pfnAlpha = (SetLayeredWindowAttributes_t)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetLayeredWindowAttributes");
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    if (pfnAlpha) {
        hw.layeredOk = pfnAlpha(hwnd, 0, 0, LWA_ALPHA);
        if (hw.layeredOk) any = true;
    }
    return any;
}

// 方案：通过 DLL 注入在目标进程内做 Cloak + WDA
static bool ApplyCloakViaInjection(HWND hwnd, HiddenWindow& hw, std::wstring& diagErr)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) { diagErr = L"无法获取窗口PID"; return false; }

    // 注入 DLL（如果还没注入）
    auto res = inject::InjectDllOnly(pid, L"MythwareHideHook");
    if (!res.success) {
        diagErr = L"注入失败: " + res.error;
        return false;
    }
    hw.injectOk = true;

    // 等待消息窗口就绪
    HWND msgWnd = nullptr;
    for (int i = 0; i < 20; i++) {
        msgWnd = FindWindowW(nullptr, L"MythwareHacker MsgCenter Window");
        if (msgWnd) break;
        Sleep(100);
    }
    if (!msgWnd) {
        diagErr = L"DLL消息窗口未就绪";
        return false;
    }

    // 发送 cloak 命令: cloak:<hwnd>:true
    wchar_t cmd[64];
    swprintf(cmd, 64, L"cloak:%llu:true", (unsigned long long)(ULONG_PTR)hwnd);
    bool ok = inject::SendCmdToMsgCenter(L"MythwareHacker MsgCenter Window", cmd);
    if (!ok) {
        diagErr = L"发送 cloak 命令失败";
        return false;
    }

    // 同时尝试直接用远程调用再保险一次
    struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
    RemoteCloakParams p1 = { hwnd, TRUE };
    auto r2 = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                     &p1, sizeof(p1));
    if (r2.success && r2.exitCode == 0) hw.cloakOk = true;

    // 远程 WDA
    struct RemoteAffinityParams { HWND hwnd; DWORD affinity; };
    RemoteAffinityParams p2 = { hwnd, WDA_EXCLUDEFROMCAPTURE };
    auto r3 = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                                     &p2, sizeof(p2));
    if (r3.success && r3.exitCode == 0) hw.wdaOk = true;

    // 远程 Layered alpha=0
    struct RemoteLayeredAlphaParams { HWND hwnd; BYTE alpha; };
    RemoteLayeredAlphaParams p3 = { hwnd, 0 };
    auto r4 = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetLayeredAlpha",
                                     &p3, sizeof(p3));
    if (r4.success && r4.exitCode == 0) hw.layeredOk = true;

    return hw.cloakOk || hw.wdaOk || hw.layeredOk;
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
    hw.origPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &hw.origPlacement);

    bool anySuccess = false;

    // 方案1（核心）：DWM Cloak + WDA + Layered 通过 DLL 注入跨进程
    {
        std::wstring err;
        if (ApplyCloakViaInjection(hwnd, hw, err)) {
            anySuccess = true;
        } else {
            diagErr = err;
        }
    }

    // 方案2（本进程窗口的降级）：直接调 Cloak + WDA
    if (!anySuccess) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) {
            if (ApplyCloakSameProcess(hwnd, hw)) anySuccess = true;
        }
    }

    // 方案3（最终兜底）：移到屏幕外
    if (!anySuccess) {
        MoveOffscreen(hwnd, hw);
        anySuccess = true;
        logger::Warn(L"Cloak/WDA均失败，使用屏幕外方案: " + hw.title + L" 诊断: " + diagErr);
        diagErr.clear();
    }

    g_hidden.push_back(hw);
    SaveToPersist();

    std::wstring methods;
    if (hw.cloakOk)     methods += L"Cloak ";
    if (hw.wdaOk)       methods += L"WDA ";
    if (hw.layeredOk)   methods += L"Layered ";
    if (hw.injectOk)    methods += L"[注入] ";
    if (hw.offscreenOk) methods += L"屏幕外";

    logger::Info(L"隐蔽窗口: " + hw.title + L" [" + hw.processName + L"] 方案: " + methods);
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

    // 反向：通过 DLL 注入恢复
    if (hw.injectOk) {
        // 发 cloak:hwnd:false 恢复
        wchar_t cmd[64];
        swprintf(cmd, 64, L"cloak:%llu:false", (unsigned long long)(ULONG_PTR)hwnd);
        inject::SendCmdToMsgCenter(L"MythwareHacker MsgCenter Window", cmd);

        // 远程 WDA_NONE
        struct RemoteAffinityParams { HWND hwnd; DWORD affinity; };
        RemoteAffinityParams p = { hwnd, WDA_NONE };
        inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetAffinity",
                              &p, sizeof(p));

        // 远程 alpha=255 恢复
        struct RemoteLayeredAlphaParams { HWND hwnd; BYTE alpha; };
        RemoteLayeredAlphaParams p2 = { hwnd, 255 };
        inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteSetLayeredAlpha",
                              &p2, sizeof(p2));
    }

    // 本进程恢复
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        auto pfnCloak = GetDwmSetWindowAttribute();
        if (pfnCloak && hw.cloakOk) {
            BOOL cloak = FALSE;
            pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        }
        auto pfnWda = GetSetWindowDisplayAffinity();
        if (pfnWda && hw.wdaOk) {
            pfnWda(hwnd, WDA_NONE);
        }
        if (hw.layeredOk) {
            typedef BOOL (WINAPI *SetLayeredWindowAttributes_t)(HWND, COLORREF, BYTE, DWORD);
            auto pfnAlpha = (SetLayeredWindowAttributes_t)GetProcAddress(
                GetModuleHandleW(L"user32.dll"), "SetLayeredWindowAttributes");
            if (pfnAlpha) pfnAlpha(hwnd, 0, 255, LWA_ALPHA);
            LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        }
    }

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
