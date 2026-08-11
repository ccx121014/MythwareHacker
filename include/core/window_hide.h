// window_hide.h - 窗口隐蔽核心模块
//
// 新方案（解决"黑色效果"问题）：
//   1. DWMWA_CLOAK + WDA 双保险（通过 DLL 注入在目标进程内调用）
//      - Cloak：让 DWM 完全停止渲染窗口 → 窗口视觉消失 + 截屏穿透（后面的内容能看到）
//      - WDA：额外保证截屏不包含该窗口
//   2. WS_EX_LAYERED + alpha=0（分层窗口完全透明）—— 兜底方案
//   3. 屏幕外移动（最终兜底，最可靠但不穿透）
//
// 为什么之前是黑色：
//   只用 WDA_EXCLUDEFROMCAPTURE → DWM 截屏时跳过该窗口，填充黑色，窗口仍正常渲染
//   用 DWMWA_CLOAK → DWM 完全移除窗口，截屏和视觉都穿透到后面的内容
#pragma once
#include "common.h"
#include "utils/persist.h"

namespace whide {

// 隐蔽窗口记录
struct HiddenWindow {
    HWND hwnd;
    std::wstring title;
    std::wstring processName;
    WINDOWPLACEMENT origPlacement = {};
    bool cloakOk = false;     // DWMWA_CLOAK 生效（核心方案）
    bool wdaOk = false;       // WDA_EXCLUDEFROMCAPTURE 生效（辅助）
    bool layeredOk = false;   // WS_EX_LAYERED + alpha=0 生效（兜底）
    bool injectOk = false;    // 通过 DLL 注入实现跨进程调用
    bool offscreenOk = false; // 屏幕外移动（最终兜底）
};

// 获取所有隐蔽窗口列表
const std::vector<HiddenWindow>& GetAll();

// 判断窗口是否已隐蔽
bool IsHidden(HWND hwnd);

// 隐蔽指定窗口
// 返回生效的方案描述（用于 UI 显示）
// diagErr 输出诊断信息（若全方案失败）
bool Hide(HWND hwnd, std::wstring& diagErr);

// 恢复指定窗口
bool Restore(HWND hwnd);

// 恢复所有隐蔽窗口
void RestoreAll();

// 切换前台窗口隐蔽状态
void ToggleCurrent();

// 保存当前隐蔽列表到持久化文件（用于崩溃恢复）
void SaveToPersist();

// 清理无效的 HWND（窗口已销毁）
void Cleanup();

} // namespace whide
