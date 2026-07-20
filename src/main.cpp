// main.cpp - MythwareHacker 主程序
//
// 集大成：JiYuTrainer + MythwareToolkit + MythwareHide 三项目功能整合
//
// 整合功能：
//   1. 窗口隐蔽（防截屏）：WDA + DLL注入 + DWM Cloak + 屏幕外移动 四套方案
//   2. 极域进程控制：杀/挂起/恢复 StudentMain、广播窗口化、退出黑屏
//   3. 驱动卸载：解除网络限制(TDNetFilter) + U盘限制(TDFileFilter)
//   4. 学生机房管理助手控制：杀进程、动态密码计算器(v9.x-v12.0)、一键解禁系统程序
//
// 兼容性：Win7+ 全兼容，32/64 双架构
//   - Win10 2004+：WDA 全功能
//   - Win7/8/旧Win10：自动降级
//   - 32位/64位：注入严格位数匹配
//
// 编译：见 Makefile 或 scripts/build.bat

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
#include "utils/log.h"
#include "utils/window_utils.h"
#include "utils/persist.h"

namespace app {
    Context g_ctx;
}

// ---------------------------------------------------------------------------
// 全局低级别鼠标钩子：标题栏右键检测 + 选择模式点击
// ---------------------------------------------------------------------------
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(app::g_ctx.hMouseHook, nCode, wParam, lParam);

    auto* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    // 选择模式：左键隐蔽/恢复，右键/中键退出
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

    // 标题栏右键检测
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

        // 标题栏菜单
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
        // 窗口列表菜单
        else if (cmd >= ID_TRAY_WINDOW_LIST_BASE) {
            HWND hwnd = menu::GetWindowFromMenuId(cmd); // 注意：menu.cpp 中定义
            if (hwnd) {
                std::wstring diag;
                if (whide::IsHidden(hwnd))
                    whide::Restore(hwnd);
                else
                    whide::Hide(hwnd, diag);
                tray::UpdateTip(hWnd);
            }
        }
        // 窗口隐蔽功能
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
        // 极域进程控制
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
        } else if (cmd == ID_TRAY_BROADCAST_FULL) {
            pctl::BroadcastToFullscreen();
        } else if (cmd == ID_TRAY_EXIT_BLACK) {
            pctl::ExitBlackScreen();
        }
        // 限制解除
        else if (cmd == ID_TRAY_UNBLOCK_NET) {
            if (MessageBoxW(hWnd, L"确认卸载 TDNetFilter 驱动以解除网络限制？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockNetwork();
                MessageBoxW(hWnd, ok ? L"网络限制已解除" : L"解除失败，请查看日志",
                            APP_TITLE, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        } else if (cmd == ID_TRAY_UNBLOCK_USB) {
            if (MessageBoxW(hWnd, L"确认卸载 TDFileFilter 驱动以解除 U 盘限制？", APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool ok = drvctl::UnblockUSB();
                MessageBoxW(hWnd, ok ? L"U 盘限制已解除" : L"解除失败，请查看日志",
                            APP_TITLE, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        }
        // 学生机房管理助手
        else if (cmd == ID_TRAY_KILL_CLASSROOM) {
            auto r = mctl::KillClassroomHelper();
            MessageBoxW(hWnd, (L"已杀掉 " + WSTR(r.killedCount) + L" 个助手进程").c_str(),
                        APP_TITLE, MB_OK | MB_ICONINFORMATION);
        } else if (cmd == ID_TRAY_CALC_PASSWORD) {
            mainwin::Show();
        } else if (cmd == ID_TRAY_RESTORE_SYS) {
            auto r = mctl::UnblockSystemPrograms();
            MessageBoxW(hWnd, r.detail.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
        }
        // 显示主界面
        else if (cmd == ID_TRAY_SHOW_GUI) {
            mainwin::Show();
        }
        // 退出
        else if (cmd == ID_TRAY_EXIT) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_DESTROY:
        whide::RestoreAll();
        persist::Delete();
        preview::Cleanup();
        floatw::Cleanup();
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

// ---------------------------------------------------------------------------
// 注册主窗口类
// ---------------------------------------------------------------------------
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
    app::g_ctx.hInst = hInst;

    // 单实例互斥锁
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            L"程序已经在运行中！\n请在系统托盘区域找到盾牌图标。",
            APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 初始化日志系统
    logger::Init();

    // 注册所有窗口类
    if (!RegisterMainClass(hInst)) return 1;
    if (!mainwin::RegisterClass(hInst)) return 1;
    if (!preview::RegisterClass(hInst)) return 1;
    if (!floatw::RegisterClass(hInst)) return 1;

    // 创建主窗口（隐藏，仅用于消息处理）
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

    // 加载光标
    app::g_ctx.hCursorCross  = LoadCursor(nullptr, IDC_CROSS);
    app::g_ctx.hCursorNormal = LoadCursor(nullptr, IDC_ARROW);

    // 初始化托盘
    tray::Add(app::g_ctx.hWndMain);

    // 注册快捷键
    hotkey::RegisterAll(app::g_ctx.hWndMain);

    // 安装全局低级鼠标钩子
    app::g_ctx.hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);

    // 定时刷新托盘 tooltip
    SetTimer(app::g_ctx.hWndMain, TRAYTIP_TIMER_ID, TRAYTIP_INTERVAL_MS, nullptr);

    // 启动时检查并恢复上次未恢复的窗口
    int restored = persist::LoadAndRestore();
    if (restored > 0) {
        std::wstring msg = L"检测到上次程序退出时未恢复的窗口，已自动恢复 " +
                           WSTR(restored) + L" 个窗口。\n\n"
                           L"建议：下次退出时使用托盘菜单的「退出」按钮，\n"
                           L"程序会自动恢复所有窗口。";
        MessageBoxW(app::g_ctx.hWndMain, msg.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION);
    }

    // 创建并显示图形界面
    mainwin::Create(hInst);
    mainwin::Show();

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    preview::Cleanup();
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
