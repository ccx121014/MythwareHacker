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
// 设置 Cloak 后验证是否真的生效，DWM 可能返回 S_OK 但不实际 cloak
static bool TryCloak(HWND hwnd, HiddenWindow& hw)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    bool isSelf = (pid == GetCurrentProcessId());

    // 验证函数：设置后等待 150ms 再检查 DWMWA_CLOAKED
    auto verifyCloak = [](HWND h, bool self) -> bool {
        Sleep(150);
        if (self) {
            HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
            if (!hDwm) return false;
            typedef HRESULT (WINAPI *DwmGetWindowAttribute_t)(HWND, DWORD, PVOID, DWORD);
            auto pfnGet = (DwmGetWindowAttribute_t)GetProcAddress(hDwm, "DwmGetWindowAttribute");
            if (!pfnGet) { FreeLibrary(hDwm); return false; }
            BOOL cloaked = FALSE;
            HRESULT hr = pfnGet(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
            FreeLibrary(hDwm);
            return SUCCEEDED(hr) && cloaked;
        }
        // 跨进程验证：调用 RemoteIsCloaked
        // RemoteIsCloaked 接受 HWND 作为参数（不是结构体）
        auto r = inject::InjectAndCall(h, L"MythwareHideHook", "RemoteIsCloaked",
                                       nullptr, 0);
        // 注意：InjectAndCall 的参数是 RemoteIsCloaked(LPVOID)，我们把 hwnd 作为参数传
        // 但 InjectAndCall 需要参数数据... 改用直接传 HWND 值
        return r.success && r.exitCode == 0;
    };

    if (isSelf) {
        auto pfnCloak = GetDwmSetWindowAttribute();
        if (!pfnCloak) return false;
        BOOL cloak = TRUE;
        HRESULT hr = pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        if (SUCCEEDED(hr)) {
            // 验证
            if (verifyCloak(hwnd, true)) {
                hw.cloakOk = true;
                return true;
            }
            // 没验证成功，重试一次
            pfnCloak(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
            if (verifyCloak(hwnd, true)) {
                hw.cloakOk = true;
                return true;
            }
        }
        return false;
    }

    // 跨进程
    struct RemoteCloakParams { HWND hwnd; BOOL cloak; };
    RemoteCloakParams p = { hwnd, TRUE };

    // 验证函数：调用 RemoteIsCloaked，传入 HWND
    auto verifyRemote = [](HWND h) -> bool {
        Sleep(150);
        auto rv = inject::InjectAndCall(h, L"MythwareHideHook", "RemoteIsCloaked",
                                        &h, sizeof(h));
        return rv.success && rv.exitCode == 0;
    };

    // 第一次尝试
    auto r = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                   &p, sizeof(p));
    if (r.success && r.exitCode == 0) {
        hw.injectOk = true;
        if (verifyRemote(hwnd)) {
            hw.cloakOk = true;
            return true;
        }
    }

    // InjectAndCall 失败，先 InjectDllOnly
    if (!hw.injectOk) {
        auto res = inject::InjectDllOnly(pid, L"MythwareHideHook");
        if (!res.success) return false;
        hw.injectOk = true;
        for (int i = 0; i < 20; i++) {
            if (FindWindowW(nullptr, L"MythwareHacker MsgCenter Window")) break;
            Sleep(100);
        }
    }

    // 重试 cloak
    auto r2 = inject::InjectAndCall(hwnd, L"MythwareHideHook", "RemoteCloakWindow",
                                    &p, sizeof(p));
    if (r2.success && r2.exitCode == 0) {
        if (verifyRemote(hwnd)) {
            hw.cloakOk = true;
            return true;
        }
    }
    return false;
}

// ============ Cloak 维持线程 ============
// DWM 可能在数秒后恢复 cloak 状态，需要定期重新设置
static HANDLE g_hCloakThread = nullptr;
static volatile bool g_cloakThreadRunning = false;

static DWORD WINAPI CloakMaintainThreadProc(LPVOID)
{
    logger::Info(L"隐蔽维持线程已启动（间隔 3000ms）");
    while (g_cloakThreadRunning) {
        Sleep(3000);
        if (!g_cloakThreadRunning) break;

        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);

        for (auto& hw : g_hidden) {
            if (!IsWindow(hw.hwnd)) continue;

            // 维持 Cloak
            if (hw.cloakOk) {
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

            // 维持屏幕外：检查窗口是否被移回来了
            if (hw.offscreenOk) {
                RECT rc;
                if (GetWindowRect(hw.hwnd, &rc)) {
                    // 窗口左上角在虚拟桌面可见范围内，说明被移回来了
                    if (rc.left < vx + vw && rc.top < vy + vh) {
                        LONG w = rc.right - rc.left;
                        LONG h = rc.bottom - rc.top;
                        int targetX, targetY;
                        CalcOffscreenTarget(targetX, targetY);

                        DWORD pid2 = 0;
                        GetWindowThreadProcessId(hw.hwnd, &pid2);
                        if (pid2 == 0 || pid2 == GetCurrentProcessId()) {
                            SetWindowPos(hw.hwnd, nullptr, targetX, targetY,
                                         w, h, SWP_NOZORDER | SWP_NOACTIVATE);
                        } else {
                            RemoteMoveParamsLayout p = { hw.hwnd, targetX, targetY,
                                                         (int)w, (int)h };
                            auto r = inject::InjectAndCall(hw.hwnd, L"MythwareHideHook",
                                "RemoteMoveOffscreen", &p, sizeof(p));
                            if (!r.success || r.exitCode != 0) {
                                SetWindowPos(hw.hwnd, nullptr, targetX, targetY,
                                             w, h, SWP_NOZORDER | SWP_NOACTIVATE);
                            }
                        }
                    }
                }
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
        WaitForSingleObject(g_hCloakThread, 3000);
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
        StartCloakMaintainThread();  // 启动维持线程防止窗口被移回来
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
