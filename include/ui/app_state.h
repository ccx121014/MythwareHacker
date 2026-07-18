// app_state.h - 全局 UI 状态
//
// 集中保存所有 UI 模块共享的状态，避免循环依赖
#pragma once
#include "common.h"

namespace app {

// 全局应用上下文
struct Context {
    HINSTANCE hInst       = nullptr;
    HWND      hWndMain    = nullptr;
    HWND      hWndPreview = nullptr;
    HWND      hWndFloat   = nullptr;
    HHOOK     hMouseHook  = nullptr;

    // 鼠标选择模式
    bool      selectMode  = false;
    HCURSOR   hCursorCross  = nullptr;
    HCURSOR   hCursorNormal = nullptr;

    // 标题栏右键的目标窗口
    HWND      lastTitleBarHWnd = nullptr;

    // 上次隐蔽诊断信息
    std::wstring lastDiag;
};

// 全局上下文（在 main.cpp 中定义）
extern Context g_ctx;

// 便捷访问
inline Context& Ctx() { return g_ctx; }

} // namespace app
