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
// 偏移量 10000px，确保任何多屏环境（含副屏在主屏右侧）都完全在屏幕外
static const int OFFSCREEN_OFFSET = 10000;

// 计算屏幕外目标坐标（虚拟桌面右下角外侧）
static void CalcOffscreenTarget(int& targetX, int& targetY)
{
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    targetX = vx + vw + OFFSCREEN_OFFSET;
    targetY = vy + vh + OFFSCREEN_OFFSET;
}

// 跨进程移动窗口的参数布局（必须与 hide_hook.cpp 的 RemoteMoveParams 一致）
struct RemoteMoveParamsLayout {
    HWND hwnd;
    int  x;
    int  y;
    int  w;
    int  h;
};

static void MoveOffscreen(HWND hwnd, HiddenWindow& hw)
{
    hw.origPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &hw.origPlacement);
    RECT rc; GetWindowRect(hwnd, &rc);
    LONG w = rc.right - rc.left;
    LONG h = rc.bottom - rc.top;

    int targetX, targetY;
    CalcOffscreenTarget(targetX, targetY);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        // 本进程窗口：直接调用
        SetWindowPos(hwnd, nullptr, targetX, targetY, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        // 跨进程：通过注入在目标进程内部调用 SetWindowPos
        // 绕过目标进程的窗口钩子对跨进程调用的拦截（极域常有全局钩子）
        RemoteMoveParamsLayout p = { hwnd, targetX, targetY, (int)w, (int)h };
        auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteMoveOffscreen",
                                       &p, sizeof(p));
        if (!r.success || r.exitCode != 0) {
            logger::Warn(L"注入调用 RemoteMoveOffscreen 失败，降级到直接调用: " +
                         hw.title + L" code=" + WSTR(r.exitCode) + L" err=" + r.error);
            SetWindowPos(hwnd, nullptr, targetX, targetY, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
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
// 直接设置 Cloak，不做二次验证（验证会引入 Sleep 延迟，导致卡顿）。
// DWMWA_CLOAK 在 Win8+ 上只要 DwmSetWindowAttribute 返回 S_OK 即生效。
// 维持线程会定期重新设置，防止 DWM 恢复。
static bool TryCloak(HWND hwnd, HiddenWindow& hw)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    bool isSelf = (pid == GetCurrentProcessId());

    if (isSelf) {
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

    // 跨进程：一次注入调用，不重试、不验证（避免卡顿）
    struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
    RemoteCloakParams p = { hwnd, TRUE };

    auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                   &p, sizeof(p));
    if (r.success && r.exitCode == 0) {
        hw.injectOk = true;
        hw.cloakOk = true;
        return true;
    }
    return false;
}

// ============ Cloak 维持线程 ============
// DWM 可能在数秒后恢复 cloak 状态，需要定期重新设置。
// 优化：间隔 5000ms，且只遍历 cloakOk 标记的窗口，避免对所有隐蔽窗口做跨进程注入。
static HANDLE g_hCloakThread = nullptr;
static volatile bool g_cloakThreadRunning = false;

static DWORD WINAPI CloakMaintainThreadProc(LPVOID)
{
    logger::Info(L"隐蔽维持线程已启动（间隔 5000ms，仅维持 Cloak 窗口）");
    while (g_cloakThreadRunning) {
        // 分段睡眠，退出时能快速响应
        for (int i = 0; i < 50 && g_cloakThreadRunning; i++) Sleep(100);
        if (!g_cloakThreadRunning) break;

        for (auto& hw : g_hidden) {
            if (!IsWindow(hw.hwnd)) continue;
            // 只维持 Cloak 标记的窗口，其他方案（WDA/屏幕外）不需要定期维持
            if (!hw.cloakOk) continue;

            DWORD pid = 0;
            GetWindowThreadProcessId(hw.hwnd, &pid);
            if (pid == GetCurrentProcessId()) {
                auto pfnCloak = GetDwmSetWindowAttribute();
                if (pfnCloak) {
                    BOOL cloak = TRUE;
                    pfnCloak(hw.hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
                }
            } else {
                struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
                RemoteCloakParams p = { hw.hwnd, TRUE };
                inject::InjectAndCall(hw.hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                      &p, sizeof(p));
            }
        }
    }
    logger::Info(L"隐蔽维持线程已停止");
    return 0;
}

static void StartCloakMaintainThread()
{
    if (g_hCloakThread) return;
    g_cloakThreadRunning = true;
    g_hCloakThread = CreateThread(nullptr, 0, CloakMaintainThreadProc, nullptr, 0, nullptr);
}

static void StopCloakMaintainThread()
{
    g_cloakThreadRunning = false;
    if (g_hCloakThread) {
        DWORD result = WaitForSingleObject(g_hCloakThread, 500);
        if (result != WAIT_OBJECT_0) {
            TerminateThread(g_hCloakThread, 0);
        }
        CloseHandle(g_hCloakThread);
        g_hCloakThread = nullptr;
    }
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
        StartCloakMaintainThread();  // 启动维持线程防止 DWM 恢复
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
        // 屏幕外方案不需要维持线程（窗口稳定停留在屏幕外）
        logger::Warn(L"Cloak和WDA均失败，降级到屏幕外: " + hw.title);
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
    StopCloakMaintainThread();
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
