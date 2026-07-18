// window_hide.h - 窗口隐蔽核心模块
//
// 四套方案按优先级递降：
//   1. WDA_EXCLUDEFROMCAPTURE（Win10 2004+，本进程窗口）
//   2. DLL 注入跨进程调用 WDA（位数需匹配）
//   3. DWMWA_CLOAK（Win8+，本进程）
//   4. 屏幕外移动（兜底，最可靠）
//
// 兼容性：
//   - Win7/8/旧Win10：方案 1/2 失败，走方案 3/4
//   - 32位/64位：注入时严格位数匹配，不匹配直接走方案 4
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
    bool wdaOk = false;       // 方案1生效
    bool injectOk = false;    // 方案2生效
    bool cloakOk = false;     // 方案3生效
    bool offscreenOk = false; // 方案4生效
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
