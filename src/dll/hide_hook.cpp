// hide_hook.cpp —— 注入到极域(StudentMain.exe)的DLL
// ==========================================================
// 功能设计（完全参考JiYuTrainer架构）：
//   1. DLL_PROCESS_ATTACH时检测是否是StudentMain.exe
//   2. 在极域进程内创建消息接收窗口（MythwareHacker MsgCenter）
//   3. 主程序通过WM_COPYDATA发送命令：fakeFull/manualFull/manualTop 等
//   4. 对广播窗口进行 WNDPROC 子类化，拦截：
//        - WM_WINDOWPOSCHANGING：从源头阻止极域的全屏/置顶死循环
//        - WM_SIZE：调整子窗口 TDDesk Render Window
//        - WM_GETMINMAXINFO：允许最小化到任意大小
//   5. 全局变量 fakeFull/gbFullManual/allowGbTop 控制钩子行为
// ==========================================================
// 编译：g++ -DBUILD_DLL -shared -o MythwareHideHook.dll hide_hook.cpp -O2 -s -luser32

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// ============== 导出类型：远程 SetWindowDisplayAffinity ==============
typedef BOOL (WINAPI *SetWindowDisplayAffinity_t)(HWND, DWORD);

struct RemoteAffinityParams {
    HWND hwnd;
    DWORD affinity;
};

extern "C" __declspec(dllexport) DWORD WINAPI RemoteSetAffinity(LPVOID lpParam)
{
    RemoteAffinityParams* params = (RemoteAffinityParams*)lpParam;
    if (!params) return 1;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
        if (!hUser32) return 4;
    }
    auto pfn = (SetWindowDisplayAffinity_t)GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    if (!pfn) return 3;
    BOOL ok = pfn(params->hwnd, params->affinity);
    return ok ? 0 : 2;
}

// ============== 全局状态（和JiYuTrainer一致的命名） ==============
static HINSTANCE g_hInst = NULL;
static HWND g_hMsgCenter = NULL;       // 消息中心窗口句柄
static HWND g_hMainWnd = NULL;         // 主程序窗口句柄（用于回发消息）
static HANDLE g_hMsgThread = NULL;     // 消息中心线程

static int  g_screenW = 0;
static int  g_screenH = 0;

// === 广播控制的核心全局开关（JiYuTrainer同款） ===
static bool g_fakeFull = false;        // 假全屏：true=允许极域全屏，false=阻止
static bool g_gbFullManual = false;    // 手动全屏模式
static bool g_allowGbTop = false;      // 是否允许广播置顶

// === 子类化相关 ===
static HWND g_gbWnd = NULL;            // 当前广播窗口
static WNDPROC g_gbOldWndProc = NULL;  // 广播窗口原始窗口过程
static HWND g_gbDeskRdWnd = NULL;      // TDDesk Render Window 子窗口
static int  g_gbToolHeight = 0;        // 广播工具栏高度（V6.0豪华版）

// 非法窗口列表（主程序枚举的极域窗口都放进来，钩子会拦截它们的位置变化）
#define MAX_ILLEGAL_HWNDS 256
static HWND g_illegalHwnds[MAX_ILLEGAL_HWNDS];
static int  g_illegalCount = 0;

static bool IsInIllegalHwnds(HWND h)
{
    for (int i = 0; i < g_illegalCount; i++)
        if (g_illegalHwnds[i] == h) return true;
    return false;
}
static void AppendIllegalHwnd(HWND h)
{
    if (IsInIllegalHwnds(h)) return;
    if (g_illegalCount < MAX_ILLEGAL_HWNDS)
        g_illegalHwnds[g_illegalCount++] = h;
}
static void ClearIllegalHwnds()
{
    g_illegalCount = 0;
}

// 判断窗口标题是否是广播窗口
static bool IsBroadcastTitle(const wchar_t* text)
{
    if (!text || !*text) return false;
    return (wcsstr(text, L"广播") != NULL) ||
           (wcsstr(text, L"演示") != NULL) ||
           (wcsstr(text, L"共享") != NULL) ||
           (wcscmp(text, L"屏幕演播室窗口") == 0);
}
static bool IsBlackScreenTitle(const wchar_t* text)
{
    return text && wcscmp(text, L"BlackScreen Window") == 0;
}
static bool IsGbOrHp(HWND h)
{
    if (!h) return false;
    wchar_t t[128] = {};
    GetWindowTextW(h, t, 128);
    return IsBroadcastTitle(t) || IsBlackScreenTitle(t);
}

// ============== 回发消息给主程序 ==============
static void SendBack(const wchar_t* msg)
{
    if (!g_hMainWnd || !IsWindow(g_hMainWnd)) return;
    COPYDATASTRUCT cds = {};
    cds.lpData = (PVOID)msg;
    cds.cbData = (DWORD)(sizeof(wchar_t) * (wcslen(msg) + 1));
    SendMessageTimeoutW(g_hMainWnd, WM_COPYDATA, (WPARAM)g_hMsgCenter,
                        (LPARAM)&cds, SMTO_ABORTIFHUNG | SMTO_NORMAL, 500, NULL);
}

// ============== 广播窗口子类化：新的 WNDPROC ==============
// 参考 JiYuTrainer JiYuWndProc
static LRESULT CALLBACK MythwareGbWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // 1) WM_GETMINMAXINFO：允许最小化到任意大小，不被极域限制
    if (uMsg == WM_GETMINMAXINFO) {
        PMINMAXINFO pmmi = (PMINMAXINFO)lParam;
        pmmi->ptMinTrackSize.x = 0;
        pmmi->ptMinTrackSize.y = 0;
        return 0;
    }

    // 2) WM_WINDOWPOSCHANGING：从源头拦截极域的全屏/置顶设置
    // 这是**最核心**的改进：极域调用 SetWindowPos 时，窗口会先收到这个消息。
    // 我们在这里修改参数，就不会出现"外部改完极域又改回去"的死循环。
    if (uMsg == WM_WINDOWPOSCHANGING) {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        // 如果假全屏=false（窗口化模式），且极域试图全屏，则拦截
        if (!g_fakeFull && !g_gbFullManual) {
            if (wp->x <= 0 && wp->y <= 0 &&
                wp->cx >= g_screenW && wp->cy >= g_screenH) {
                // 改为默认的窗口大小（3/4宽 × 4/5高，居中）
                int w = (int)((double)g_screenW * 3.0 / 4.0);
                int h = (int)((double)g_screenH * 4.0 / 5.0);
                wp->x = (g_screenW - w) / 2;
                wp->y = (g_screenH - h) / 2;
                wp->cx = w;
                wp->cy = h;
                // 去掉 z-order 的 TOPMOST
                if (!(wp->flags & SWP_NOZORDER)) {
                    wp->hwndInsertAfter = HWND_NOTOPMOST;
                }
            }
        }
        // 如果不允许置顶，则去掉 TOPMOST
        if (!g_allowGbTop && !g_fakeFull && !g_gbFullManual) {
            if (!(wp->flags & SWP_NOZORDER) &&
                (wp->hwndInsertAfter == HWND_TOPMOST)) {
                wp->hwndInsertAfter = HWND_NOTOPMOST;
            }
        }
        // 继续原处理
    }

    // 3) WM_SIZE：调整 TDDesk Render Window 子窗口大小
    if (uMsg == WM_SIZE) {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        // 查找/缓存 TDDesk Render Window
        if (g_gbDeskRdWnd == NULL || GetParent(g_gbDeskRdWnd) != hWnd) {
            g_gbDeskRdWnd = FindWindowExW(hWnd, NULL, NULL, L"TDDesk Render Window");
            // 查找工具栏（V6.0 豪华版）
            if (g_gbDeskRdWnd) {
                HWND tool = FindWindowExW(hWnd, g_gbDeskRdWnd, NULL, L"");
                if (tool) {
                    RECT rt; GetClientRect(tool, &rt);
                    g_gbToolHeight = rt.bottom - rt.top;
                }
            }
        }
        if (g_gbDeskRdWnd) {
            int offY = (g_fakeFull || g_gbFullManual) ? 0 : g_gbToolHeight;
            MoveWindow(g_gbDeskRdWnd, 0, offY,
                       rcClient.right - rcClient.left,
                       rcClient.bottom - rcClient.top - offY, TRUE);
        }
        // 不返回，继续调用原始过程
    }

    // 4) WM_SHOWWINDOW：显示时调整为默认窗口大小
    if (uMsg == WM_SHOWWINDOW) {
        if (wParam && !g_fakeFull && !g_gbFullManual) {
            int w = (int)((double)g_screenW * 3.0 / 4.0);
            int h = (int)((double)g_screenH * 4.0 / 5.0);
            SetWindowPos(hWnd, NULL, (g_screenW - w) / 2, (g_screenH - h) / 2,
                         w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // 5) WM_DESTROY：取消子类化
    if (uMsg == WM_DESTROY) {
        if (g_gbWnd == hWnd) {
            SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)g_gbOldWndProc);
            g_gbWnd = NULL;
            g_gbOldWndProc = NULL;
            g_gbDeskRdWnd = NULL;
        }
    }

    return CallWindowProcW(g_gbOldWndProc, hWnd, uMsg, wParam, lParam);
}

// ============== 子类化广播窗口（参考 JiYuTrainer VFixGuangBoWindow） ==============
static void HookBroadcastWindow(HWND hWnd)
{
    if (!hWnd || !IsWindow(hWnd)) return;
    if (g_gbWnd == hWnd && g_gbOldWndProc != NULL) return; // 已Hook

    // 如果已有hook的旧窗口，先恢复
    if (g_gbWnd && g_gbOldWndProc) {
        SetWindowLongPtrW(g_gbWnd, GWLP_WNDPROC, (LONG_PTR)g_gbOldWndProc);
    }

    g_gbWnd = hWnd;

    // 替换窗口过程（子类化）
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtrW(hWnd, GWLP_WNDPROC);
    if (oldProc != MythwareGbWndProc) {
        g_gbOldWndProc = oldProc;
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)MythwareGbWndProc);
    }

    // 加上 WS_OVERLAPPEDWINDOW 使其可拖动可调整大小
    LONG style = GetWindowLongW(hWnd, GWL_STYLE);
    if ((style & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW)
        style |= WS_OVERLAPPEDWINDOW;
    if ((style & WS_SYSMENU) != WS_SYSMENU)
        style |= WS_SYSMENU;
    SetWindowLongW(hWnd, GWL_STYLE, style);

    // 去除 CS_NOCLOSE（允许关闭）
    LONG clsStyle = GetClassLongW(hWnd, GCL_STYLE);
    if (clsStyle & CS_NOCLOSE) {
        SetClassLongW(hWnd, GCL_STYLE, clsStyle & ~CS_NOCLOSE);
    }
}

// ============== 处理来自主程序的消息 ==============
static void HandleCommand(wchar_t* buf)
{
    if (!buf || !*buf) return;
    wchar_t* ctx = NULL;
    wchar_t* cmd = wcstok_s(buf, L":", &ctx);
    if (!cmd) return;
    wchar_t* a1 = wcstok_s(NULL, L":", &ctx);
    wchar_t* a2 = wcstok_s(NULL, L":", &ctx);
    wchar_t* a3 = wcstok_s(NULL, L":", &ctx);

    if (wcscmp(cmd, L"init") == 0 && a1) {
        // init:<主窗口HWND数字>
        g_hMainWnd = (HWND)(ULONG_PTR)wcstoul(a1, nullptr, 0);
        SendBack(L"ok:init");
    }
    else if (wcscmp(cmd, L"hwnd") == 0 && a1) {
        // hwnd:<广播窗口HWND> ：通知DLL子类化这个广播窗口
        HWND h = (HWND)(ULONG_PTR)wcstoul(a1, nullptr, 0);
        AppendIllegalHwnd(h);
        HookBroadcastWindow(h);
        SendBack(L"ok:hwnd");
    }
    else if (wcscmp(cmd, L"hwf") == 0 && a1) {
        // hwf:<HWND> ：和hwnd一样
        HWND h = (HWND)(ULONG_PTR)wcstoul(a1, nullptr, 0);
        AppendIllegalHwnd(h);
        if (IsGbOrHp(h)) HookBroadcastWindow(h);
        SendBack(L"ok:hwf");
    }
    else if (wcscmp(cmd, L"clearhwnd") == 0) {
        ClearIllegalHwnds();
        SendBack(L"ok:clearhwnd");
    }
    else if (wcscmp(cmd, L"fakeFull") == 0 && a1) {
        // fakeFull:true/false —— 对应 JiYuTrainer hk:fkfull
        g_fakeFull = (wcscmp(a1, L"true") == 0);
        // 立即调整当前广播窗口
        if (g_gbWnd && IsWindow(g_gbWnd)) {
            LONG style = GetWindowLongW(g_gbWnd, GWL_STYLE);
            if (g_fakeFull) {
                // 假全屏：去边框 + 置顶 + 全屏尺寸
                style &= ~(WS_BORDER | WS_OVERLAPPEDWINDOW);
                style |= WS_POPUP;
                SetWindowLongW(g_gbWnd, GWL_STYLE, style);
                LONG ex = GetWindowLongW(g_gbWnd, GWL_EXSTYLE);
                SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
                SetWindowPos(g_gbWnd, HWND_TOPMOST, 0, 0, g_screenW, g_screenH,
                             SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                SendMessageW(g_gbWnd, WM_SIZE, 0, MAKELPARAM(g_screenW, g_screenH));
            } else {
                // 窗口化：加边框 + 非置顶 + 默认尺寸
                style |= (WS_BORDER | WS_OVERLAPPEDWINDOW);
                style &= ~WS_POPUP;
                SetWindowLongW(g_gbWnd, GWL_STYLE, style);
                LONG ex = GetWindowLongW(g_gbWnd, GWL_EXSTYLE);
                if (!g_gbFullManual && !g_allowGbTop)
                    SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
                int w = (int)((double)g_screenW * 3.0 / 4.0);
                int h = (int)((double)g_screenH * 4.0 / 5.0);
                SetWindowPos(g_gbWnd, HWND_NOTOPMOST,
                             (g_screenW - w) / 2, (g_screenH - h) / 2,
                             w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                SendMessageW(g_gbWnd, WM_SIZE, 0, MAKELPARAM(w, h));
            }
            SetWindowPos(g_gbWnd, 0, 0, 0, 0, 0,
                         SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);
        }
        SendBack(g_fakeFull ? L"ok:fakeFull:true" : L"ok:fakeFull:false");
    }
    else if (wcscmp(cmd, L"manualFull") == 0 && a1) {
        // manualFull:true/false —— 手动全屏
        g_gbFullManual = (wcscmp(a1, L"true") == 0);
        if (g_gbWnd && IsWindow(g_gbWnd)) {
            LONG style = GetWindowLongW(g_gbWnd, GWL_STYLE);
            if (g_gbFullManual) {
                style &= ~(WS_BORDER | WS_OVERLAPPEDWINDOW);
                style |= (WS_POPUP | WS_SYSMENU);
                SetWindowLongW(g_gbWnd, GWL_STYLE, style);
                LONG ex = GetWindowLongW(g_gbWnd, GWL_EXSTYLE);
                SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
                SetWindowPos(g_gbWnd, HWND_TOPMOST, 0, 0, g_screenW, g_screenH,
                             SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                SendMessageW(g_gbWnd, WM_SIZE, 0, MAKELPARAM(g_screenW, g_screenH));
            } else {
                style |= (WS_BORDER | WS_OVERLAPPEDWINDOW);
                style &= ~WS_POPUP;
                SetWindowLongW(g_gbWnd, GWL_STYLE, style);
                LONG ex = GetWindowLongW(g_gbWnd, GWL_EXSTYLE);
                if (!g_allowGbTop)
                    SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
                int w = (int)((double)g_screenW * 3.0 / 4.0);
                int h = (int)((double)g_screenH * 4.0 / 5.0);
                SetWindowPos(g_gbWnd, HWND_NOTOPMOST,
                             (g_screenW - w) / 2, (g_screenH - h) / 2,
                             w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                SendMessageW(g_gbWnd, WM_SIZE, 0, MAKELPARAM(w, h));
            }
        }
        SendBack(g_gbFullManual ? L"ok:manualFull:true" : L"ok:manualFull:false");
    }
    else if (wcscmp(cmd, L"manualTop") == 0 && a1) {
        // manualTop:true/false —— 手动置顶
        g_allowGbTop = (wcscmp(a1, L"true") == 0);
        if (g_gbWnd && IsWindow(g_gbWnd)) {
            LONG ex = GetWindowLongW(g_gbWnd, GWL_EXSTYLE);
            if (g_allowGbTop) {
                SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
                SetWindowPos(g_gbWnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            } else {
                SetWindowLongW(g_gbWnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
                SetWindowPos(g_gbWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
        SendBack(g_allowGbTop ? L"ok:manualTop:true" : L"ok:manualTop:false");
    }
    else if (wcscmp(cmd, L"unlockInput") == 0) {
        // 解除极域的键盘锁定：调用 LibTDMaster 的 UnLockLocalInput / DoneHook
        HMODULE hTDMaster = GetModuleHandleW(L"LibTDMaster.dll");
        if (hTDMaster) {
            typedef HMODULE (*fnUnLock)();
            typedef HHOOK   (*fnDone)();
            fnUnLock p1 = (fnUnLock)GetProcAddress(hTDMaster, "UnLockLocalInput");
            fnDone   p2 = (fnDone)  GetProcAddress(hTDMaster, "DoneHook");
            if (p1) p1();
            else if (p2) p2();
        }
        SendBack(L"ok:unlockInput");
    }
    else if (wcscmp(cmd, L"boom") == 0) {
        // 直接让极域崩溃退出
        PostQuitMessage(0);
        ExitProcess(0);
    }
}

// ============== 消息中心窗口过程 ==============
static INT_PTR CALLBACK MsgCenterWndProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG) {
        SetWindowTextW(hDlg, L"MythwareHacker MsgCenter Window");
        return TRUE;
    }
    if (uMsg == WM_COPYDATA) {
        PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
        if (pcds && pcds->cbData > 0) {
            wchar_t recv[512] = {};
            size_t n = pcds->cbData / sizeof(wchar_t);
            if (n > 511) n = 511;
            memcpy(recv, pcds->lpData, n * sizeof(wchar_t));
            recv[n] = 0;
            HandleCommand(recv);
        }
        return TRUE;
    }
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return FALSE;
}

// ============== 消息中心线程（DLL注入后启动） ==============
static DWORD WINAPI MsgCenterThreadProc(LPVOID)
{
    // 如果不是极域进程，直接退出
    wchar_t mod[MAX_PATH] = {};
    GetModuleFileNameW(NULL, mod, MAX_PATH);
    const wchar_t* name = wcsrchr(mod, L'\\');
    name = name ? name + 1 : mod;
    if (wcscmp(name, L"StudentMain.exe") != 0) {
        return 1;
    }

    // 检查 LibTDMaster.dll 存在（确认真的是极域）
    if (GetModuleHandleW(L"LibTDMaster.dll") == NULL) {
        return 2;
    }

    g_screenW = GetSystemMetrics(SM_CXSCREEN);
    g_screenH = GetSystemMetrics(SM_CYSCREEN);

    // 注册一个窗口类（使用简单的窗口而不是 DialogBox，避免资源依赖）
    wchar_t clsName[] = L"MYTHWARE_MSG_CENTER_CLS";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = (WNDPROC)MsgCenterWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = clsName;
    RegisterClassExW(&wc);

    g_hMsgCenter = CreateWindowExW(0, clsName, L"MythwareHacker MsgCenter Window",
                                   0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInst, NULL);
    if (!g_hMsgCenter) {
        return 3;
    }

    // 通知主程序"DLL已就绪"（先把消息放到回收站，主程序初始化后会主动发init:）
    // 我们只需要进入消息循环，主程序会通过 FindWindow 找到我们
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_hMsgCenter = NULL;
    return 0;
}

// ============== DllMain ==============
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)lpReserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);

        // 判断宿主是否是 StudentMain.exe
        wchar_t mod[MAX_PATH] = {};
        GetModuleFileNameW(NULL, mod, MAX_PATH);
        const wchar_t* name = wcsrchr(mod, L'\\');
        name = name ? name + 1 : mod;

        if (wcscmp(name, L"StudentMain.exe") == 0) {
            // 启动消息中心线程
            g_hMsgThread = CreateThread(NULL, 0, MsgCenterThreadProc, NULL, 0, NULL);
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_hMsgCenter) {
            DestroyWindow(g_hMsgCenter);
            g_hMsgCenter = NULL;
        }
        if (g_hMsgThread) {
            CloseHandle(g_hMsgThread);
            g_hMsgThread = NULL;
        }
    }
    return TRUE;
}
