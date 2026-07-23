// mythware_control.h - 学生机房管理助手控制
//
// 兼容 v6.8 ~ v12.99 版本的学生机房管理助手
// 功能：
//   - 杀掉助手进程（固定进程名 + 动态生成名 + 扫描回退 + 服务删除）
//   - 一键解禁系统程序：CMD、注册表编辑器、任务管理器、浏览器下载限制等
//   - 重启资源管理器
#pragma once
#include "common.h"

namespace mctl {

// 已知的学生机房管理助手固定进程名（定义在 .cpp 中，去掉 .exe 后缀比较）
extern const std::vector<std::wstring> CLASSROOM_PROCESS_NAMES;

// 杀掉学生机房管理助手进程
//   1. 杀固定进程名
//   2. 生成动态进程名并杀掉（v9.x ~ v12.x 基于 VB 随机数引擎）
//   3. 扫描回退（按字符范围识别未知动态进程名）
//   4. 停止并删除 zmserv 服务
struct KillResult {
    int killedCount;
    std::vector<std::wstring> killedNames;
};
KillResult KillClassroomHelper();

// 一键解禁系统程序
//   通过修改/删除注册表键值来解禁：
//   - CMD (DisableCMD)
//   - 注册表编辑器 (DisableRegistryTools)
//   - 任务管理器 (DisableTaskMgr)
//   - 浏览器下载限制（IE / Chrome / Edge / Firefox）
//   - USB 存储驱动
//   - IFEO debugger 劫持
//   - hosts 文件
struct UnblockSysResult {
    bool cmd;
    bool regedit;
    bool taskmgr;
    bool explorerDownloads;
    bool chromeDownloads;
    bool firefoxDownloads;
    bool usbDriver;
    bool ifeo;
    bool hosts;
    std::wstring detail;
};
UnblockSysResult UnblockSystemPrograms();

// 重启资源管理器
bool RestartExplorer();

// 机房助手持续监控线程（解决杀完后1秒又重启的问题）
// KillClassroomHelper 成功后会自动启动监控
void StartClassroomMonitor();
void StopClassroomMonitor();
bool IsClassroomMonitorRunning();

} // namespace mctl
