// process_control.h - 极域进程控制
//
// 功能：
//   - 不依赖 taskkill/ntsd 杀掉极域进程（用 OpenProcess + TerminateProcess）
//   - 启动极域（从注册表读取路径，降权到登录用户）
//   - 挂起（冻结）/恢复极域进程
//   - 广播窗口化/全屏化（查找极域广播窗口，修改其样式）
//   - 退出黑屏安静（4 级递进）
//   - 显示极域存活状态
#pragma once
#include "common.h"

namespace pctl {

// 极域进程状态
enum class MythwareState {
    NotRunning,    // 未运行
    Running,       // 正常运行
    Suspended,     // 已挂起
    NoResponse,    // 无响应
};

struct MythwareStatus {
    MythwareState state;
    DWORD pid;
    std::wstring version;
    std::wstring exePath;
};

// 获取极域 StudentMain.exe 状态
MythwareStatus GetMythwareStatus();

// 杀掉极域进程（不依赖 taskkill）
// 返回是否成功
bool KillMythware();

// 启动极域（从注册表读取路径，降权到登录用户）
bool StartMythware();

// 挂起（冻结）极域进程
bool SuspendMythware();

// 恢复极域进程
bool ResumeMythware();

// 广播窗口化（把极域全屏广播改为窗口模式）
bool BroadcastToWindowed();

// 广播全屏化（恢复全屏）
bool BroadcastToFullscreen();

// 广播窗口置顶开关
// enable=true 设为置顶，enable=false 取消置顶
bool SetBroadcastTopmost(bool enable);

// 获取广播窗口当前是否置顶
bool IsBroadcastTopmost();

// 退出黑屏安静（4 级递进）
//   级别1：隐藏黑屏窗口
//   级别2：最小化
//   级别3：发送 ESC
//   级别4：确认杀进程
bool ExitBlackScreen();

// 检查黑屏窗口是否正在显示（用于自动监控）
bool IsBlackScreenActive();

// 将极域相关窗口降级（取消置顶），使本程序窗口能正常置顶
// 这不会影响极域功能，只是让它不再抢占前台
void DemoteMythwareWindows();

// 在极域进程内创建远程线程调用 BlockInput(FALSE)，
// 绕过极域通过 BlockInput 禁用键盘。返回是否成功。
bool UnblockInputInMythware();

// 清理广播置顶维持线程（程序退出时调用）
void CleanupBroadcastTopmost();

} // namespace pctl
