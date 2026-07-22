#include "common.h"
#include "ui/app_state.h"
#include "ui/tray.h"
#include "ui/menu.h"
#include "ui/hotkey.h"
#include "ui/float_window.h"
#include "ui/preview.h"
#include "ui/main_window.h"
#include "core/window_hide.h"
#include "core/process_control.h"
#include "core/driver_control.h"
#include "core/mythware_control.h"
#include "core/password_calc.h"
#include "core/inject.h"
#include "core/self_protect.h"
#include "utils/log.h"
#include "utils/window_utils.h"
#include "utils/persist.h"

namespace app {
    Context g_ctx;
}

// ---------------------------------------------------------------------------
// 崩溃诊断日志
// ---------------------------------------------------------------------------
#include <imagehlp.h>

static LONG WINAPI MyUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    WCHAR crashDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, crashDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(crashDir, L'\\');
    if (lastSlash) *lastSlash = 0;
    wcscat(crashDir, L"\\crash_report.txt");

    HANDLE hFile = CreateFileW(crashDir, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!hFile) return EXCEPTION_EXECUTE_HANDLER;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeBuf[64] = {};
    swprintf(timeBuf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring report = L"===== MythwareHacker 崩溃报告 =====\r\n";
    report += L"时间: " + std::wstring(timeBuf) + L"\r\n";
    report += L"版本: " + std::wstring(APP_VERSION) + L"\r\n";
    report += L"系统: ";
    if (common::IsWin10Build19041OrLater()) report += L"Win10 2004+\r\n";
    else report += L"旧版 Windows\r\n";
    report += L"位数: ";
    report += common::IsSelf64Bit() ? L"64位" : L"32位";
    report += L"\r\n";

    if (ep) {
        report += L"\r\n===== 异常信息 =====\r\n";
        report += L"异常码: 0x" + WSTR((long long)ep->ExceptionRecord->ExceptionCode) + L"\r\n";
        report += L"异常地址: 0x" + WSTR((long long)ep->ExceptionRecord->ExceptionAddress) + L"\r\n";

        report += L"\r\n===== 堆栈回溯 =====\r\n";
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 sf = {};
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrPC.Offset = (DWORD64)ep->ExceptionRecord->ExceptionAddress;
        sf.AddrStack.Mode = AddrModeFlat;
        sf.AddrFrame.Mode = AddrModeFlat;
#ifdef _M_X64
        sf.AddrStack.Offset = (DWORD64)ctx.Rsp;
        sf.AddrFrame.Offset = (DWORD64)ctx.Rbp;
#else
        sf.AddrStack.Offset = (DWORD64)ctx.Esp;
        sf.AddrFrame.Offset = (DWORD64)ctx.Ebp;
#endif

        HANDLE hProc = GetCurrentProcess();
        HANDLE hThread = GetCurrentThread();
        DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
        if (!common::IsSelf64Bit()) machineType = IMAGE_FILE_MACHINE_I386;

        for (int i = 0; i < 30; i++) {
            if (!StackWalk64(machineType, hProc, hThread, &sf, &ctx,
                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }

            wchar_t symbolBuf[512] = {};
            IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*)malloc(sizeof(IMAGEHLP_SYMBOL64) + 512);
            if (symbol) {
                symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
                symbol->MaxNameLength = 512;
                DWORD64 disp = 0;
                if (SymGetSymFromAddr64(hProc, sf.AddrPC.Offset, &disp, symbol)) {
                    swprintf(symbolBuf, 512, L"%S + 0x%llX", symbol->Name, (unsigned long long)disp);
                } else {
                    swprintf(symbolBuf, 512, L"0x%llX", (unsigned long long)sf.AddrPC.Offset);
                }
                free(symbol);
            }

            report += L"[" + WSTR(i) + L"] " + std::wstring(symbolBuf) + L"\r\n";
        }
    }

    report += L"\r\n===== 系统信息 =====\r\n";
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionExW((LPOSVERSIONINFOW)&osvi);
    report += L"Windows 版本: " + WSTR(osvi.dwMajorVersion) + L"." + WSTR(osvi.dwMinorVersion) +
              L" Build " + WSTR(osvi.dwBuildNumber) + L"\r\n";

    DWORD procId = GetCurrentProcessId();
    report += L"进程ID: " + WSTR(procId) + L"\r\n";

    DWORD written = 0;
    WriteFile(hFile, report.c_str(), (DWORD)(report.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);

    logger::Error(L"程序崩溃！报告已保存到: " + std::wstring(crashDir));
    MessageBoxW(nullptr, (L"程序崩溃！报告已保存到: " + std::wstring(crashDir)).c_str(),
                L"崩溃", MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------------------------------------------------------------------------
// 全局低级别鼠标钩子
// ---------------------------------------------------------------------------
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(app::g_ctx.hMouseHook, nCode, wParam, lParam);

    auto* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    if (app::g_ctx.selectMode) {
        if (wParam == WM_LBUTTONDOWN) {
            POINT pt = p->pt;
            HWND hwnd = WindowFromPoint(pt);
            if (hwnd) {
                HWND root = GetAncestor(hwnd, GA_ROOT);
                if (root && root != app::g_ctx.hWndMain) {
                    std::wstring diag;
                    if (whide::IsHidden(root))
                        whide::Restore(root);
                    else
                        whide::Hide(root, diag);
                }
            }
            app::g_ctx.selectMode = false;
            SetCursor(app::g_ctx.hCursorNormal);
            return 1;
        }
        if (wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
            app::g_ctx.selectMode = false;
            SetCursor(app::g_ctx.hCursorNormal);
            return 1;
        }
        return CallNextHookEx(app::g_ctx.hMouseHook, nCode, wParam, lParam);
    }

    if (wParam == WM_RBUTTONUP) {
        HWND targetHwnd = nullptr;
        if (wutil::IsPointOnTitleBar(p->pt, &targetHwnd) &&
            targetHwnd &&
            wutil::IsWindowEligible(targetHwnd)) {
            app::g_ctx.lastTitleBarHWnd = targetHwnd;
            PostMessage(app::g_ctx.hWndMain, WM_TITLEBAR_RIGHTCLICK, 0, 0);
        }
    }

    return CallNextHookEx(app::g_ctx.hMouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 自动监控极域进程
// ---------------------------------------------------------------------------
static HANDLE g_hMonitorThread = nullptr;
static bool g_monitorRunning = false;
static bool g_autoBroadcastWindowed = false;

static DWORD WINAPI MonitorThreadProc(LPVOID)
{
    while (g_monitorRunning) {
        auto st = pctl::GetMythwareStatus();
        static pctl::MythwareState lastState = pctl::MythwareState::NotRunning;
        static bool wasBlackScreen = false;

        if (st.state != lastState) {
            switch (st.state) {
            case pctl::MythwareState::NotRunning:
                logger::Info(L"极域监控: 已关闭");
                break;
            case pctl::MythwareState::Running:
                logger::Info(L"极域监控: 正在运行");
                break;
            case pctl::MythwareState::Suspended:
                logger::Info(L"极域监控: 已挂起");
                break;
            case pctl::MythwareState::NoResponse:
                logger::Info(L"极域监控: 无响应");
                break;
            }
            lastState = st.state;
        }

        if (g_autoBroadcastWindowed && st.state == pctl::MythwareState::Running) {
            bool isBlack = pctl::IsBlackScreenActive();
            if (isBlack && !wasBlackScreen) {
                pctl::BroadcastToWindowed();
                logger::Info(L"极域监控: 自动将广播窗口化");
            }
            wasBlackScreen = isBlack;
        }

        // 3 秒间隔（JiYuTrainer 用 3100ms），2 秒太频繁
        Sleep(3000);
    }
    return 0;
}

static void StartMonitor()
{
    if (g_hMonitorThread) return;
    g_monitorRunning = true;
    g_hMonitorThread = CreateThread(nullptr, 0, MonitorThreadProc, nullptr, 0, nullptr);
    logger::Info(L"极域监控线程已启动");
}

static void StopMonitor()
{
    g_monitorRunning = false;
    if (g_hMonitorThread) {
        DWORD result = WaitForSingleObject(g_hMonitorThread, 500);
        if (result != WAIT_OBJECT_0) {
            TerminateThread(g_hMonitorThread, 0);
        }
        CloseHandle(g_hMonitorThread);
        g_hMonitorThread = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 主窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_TIMER:
        if (wParam == TRAYTIP_TIMER_ID) {
            tray::UpdateTip(hWnd);
        }
        return 0;

    case WM_HOTKEY:
        if (hotkey::Handle(wParam)) {
            tray::UpdateTip(hWnd);
        }
        return 0;

    case WM_TITLEBAR_RIGHTCLICK:
        if (app::g_ctx.lastTitleBarHWnd && IsWindow(app::g_ctx.lastTitleBarHWnd)) {
            menu::ShowTitleBarMenu(app::g_ctx.lastTitleBarHWnd);
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            tray::ShowContextMenu(hWnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            mainwin::Toggle();
        }
        break;

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);

        if (cmd == ID_TITLEBAR_HIDE) {
            if (app::g_ctx.lastTitleBarHWnd && IsWindow(app::g_ctx.lastTitleBarHWnd)) {
                std::wstring diag;
                if (whide::Hide(app::g_ctx.lastTitleBarHWnd, diag) && !diag.empty()) {
                    MessageBoxW(hWnd, diag.c_str(), L"诊断信息", MB_OK | MB_ICONWARNING);
                }
            }
            tray::UpdateTip(hWnd);
        } else if (cmd == ID_TITLEBAR_RESTORE) {
            if (app::g_ctx.lastTitleBarHWnd && IsWindow(app::g_ctx.lastTitleBarHWnd)) {
                whide::Restore(app::g_ctx.lastTitleBarHWnd);
            }
            tray::UpdateTip(hWnd);
        }
        else if (cmd >= ID_TRAY_WINDOW_LIST_BASE) {
            HWND hwnd = menu::GetWindowFromMenuId(cmd);
            if (hwnd) {
                std::wstring diag;
                if (whide::IsHidden(hwnd))
                    whide::Restore(hwnd);
                else
                    whide::Hide(hwnd, diag);
                tray::UpdateTip(hWnd);
            }
        }
        else if (cmd == ID_TRAY_HIDE_CURRENT) {
            whide::ToggleCurrent();
            tray::UpdateTip(hWnd);
        } else if (cmd == ID_TRAY_SELECT_MODE) {
            app::g_ctx.selectMode = true;
            SetCursor(app::g_ctx.hCursorCross);
            ShowWindow(hWnd, SW_HIDE);
        } else if (cmd == ID_TRAY_RESTORE_ALL) {
            whide::RestoreAll();
            tray::UpdateTip(hWnd);
        } else if (cmd == ID_TRAY_PREVIEW) {
            preview::Toggle();
        } else if (cmd == ID_TRAY_FLOAT_TOGGLE) {
            floatw::Toggle();
        }
        else if (cmd == ID_TRAY_KILL_MYTHWARE) {
            if (MessageBoxW(hWnd, L"确认强杀极域进程？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                pctl::KillMythware();
                tray::UpdateTip(hWnd);
            }
        } else if (cmd == ID_TRAY_SUSPEND_MYTHWARE) {
            pctl::SuspendMythware();
        } else if (cmd == ID_TRAY_RESUME_MYTHWARE) {
            pctl::ResumeMythware();
        } else if (cmd == ID_TRAY_BROADCAST_WIN) {
            pctl::BroadcastToWindowed();
        } else if (cmd == ID_TRAY_BROADCAST_TOPMOST) {
            bool current = pctl::IsBroadcastTopmost();
            pctl::SetBroadcastTopmost(!current);
        } else if (cmd == ID_TRAY_BROADCAST_FULL) {
            pctl::BroadcastToFullscreen();
        } else if (cmd == ID_TRAY_EXIT_BLACK) {
            pctl::ExitBlackScreen();
        }
        else if (cmd == ID_TRAY_UNBLOCK_NET) {
            if (MessageBoxW(hWnd, L"确认卸载 TDNetFilter 驱动以解除网络限制？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockNetwork();
                MessageBoxW(hWnd, ok ? L"网络限制已解除" : L"解除失败，请查看日志",
                            APP_TITLE, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        } else if (cmd == ID_TRAY_UNBLOCK_USB) {
            if (MessageBoxW(hWnd, L"确认卸载 TDFileFilter 驱动以解除 U 盘限制？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockUSB();
                MessageBoxW(hWnd, ok ? L"U盘限制已解除" : L"解除失败，请查看日志",
                            APP_TITLE, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        }
        else if (cmd == ID_TRAY_KILL_CLASSROOM) {
            auto r = mctl::KillClassroomHelper();
            MessageBoxW(hWnd, (L"已杀掉 " + WSTR(r.killedCount) + L" 个助手进程").c_str(),
                        APP_TITLE, MB_OK | MB_ICONINFORMATION);
        } else if (cmd == ID_TRAY_CALC_PASSWORD) {
            mainwin::Show();
        } else if (cmd == ID_TRAY_RESTORE_SYS) {
            auto r = mctl::UnblockSystemPrograms();
            MessageBoxW(hWnd, r.detail.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
        } else if (cmd == ID_TRAY_RESTART_EXPLORER) {
            mctl::RestartExplorer();
        }
        else if (cmd == ID_TRAY_SHOW_GUI) {
            mainwin::Show();
        }
        else if (cmd == ID_TRAY_EXIT) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_DESTROY:
        StopMonitor();
        spctl::DisableSelfProtect();
        whide::RestoreAll();
        persist::Delete();
        preview::Cleanup();
        floatw::Cleanup();
        pctl::CleanupBroadcastTopmost();
        KillTimer(hWnd, TRAYTIP_TIMER_ID);
        hotkey::UnregisterAll(hWnd);
        if (app::g_ctx.hMouseHook) {
            UnhookWindowsHookEx(app::g_ctx.hMouseHook);
            app::g_ctx.hMouseHook = nullptr;
        }
        tray::Remove(hWnd);
        logger::Info(L"===== 程序退出 =====");
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

static ATOM RegisterMainClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_MAIN_CLASS;
    wc.hIcon         = LoadIcon(nullptr, IDI_SHIELD);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    return RegisterClassExW(&wc);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

    app::g_ctx.hInst = hInst;

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            L"程序已经在运行中！\n请在系统托盘区域找到盾牌图标。",
            APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    logger::Init();

    if (!RegisterMainClass(hInst)) return 1;
    if (!mainwin::RegisterClass(hInst)) return 1;
    if (!preview::RegisterClass(hInst)) return 1;
    if (!floatw::RegisterClass(hInst)) return 1;

    app::g_ctx.hWndMain = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        APP_MAIN_CLASS,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        nullptr, nullptr, hInst, nullptr);
    if (!app::g_ctx.hWndMain) return 1;

    ShowWindow(app::g_ctx.hWndMain, SW_HIDE);
    UpdateWindow(app::g_ctx.hWndMain);

    app::g_ctx.hCursorCross  = LoadCursor(nullptr, IDC_CROSS);
    app::g_ctx.hCursorNormal = LoadCursor(nullptr, IDC_ARROW);

    tray::Add(app::g_ctx.hWndMain);

    hotkey::RegisterAll(app::g_ctx.hWndMain);

    app::g_ctx.hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);

    SetTimer(app::g_ctx.hWndMain, TRAYTIP_TIMER_ID, TRAYTIP_INTERVAL_MS, nullptr);

    int restored = persist::LoadAndRestore();
    if (restored > 0) {
        std::wstring msg = L"检测到上次程序退出时未恢复的窗口，已自动恢复 " +
                           WSTR(restored) + L" 个窗口。\n\n"
                           L"建议：下次退出时使用托盘菜单的「退出」按钮，\n"
                           L"程序会自动恢复所有窗口。";
        MessageBoxW(app::g_ctx.hWndMain, msg.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
    }

    mainwin::Create(hInst);
    mainwin::Show();

    StartMonitor();
    spctl::EnableSelfProtect();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    preview::Cleanup();
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    SymCleanup(GetCurrentProcess());
    return static_cast<int>(msg.wParam);
}