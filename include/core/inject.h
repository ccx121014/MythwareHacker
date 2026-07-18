// inject.h - DLL 注入通用模块
//
// 通用 DLL 注入：CreateRemoteThread + LoadLibraryW
// 注入后通过偏移计算远程函数地址并调用
#pragma once
#include "common.h"

namespace inject {

// 注入结果
struct Result {
    bool success;
    DWORD exitCode;        // 远程函数返回值
    std::wstring error;    // 失败时的诊断信息
};

// 注入 DLL 并调用导出函数
//   hwnd         目标窗口（用于获取 PID）
//   dllName      DLL 文件名（如 L"MythwareHideHook_x64.dll"）
//   funcName     导出函数名（如 "RemoteSetAffinity"）
//   params       参数数据指针
//   paramsSize   参数大小
//   outExitCode  远程函数返回值
Result InjectAndCall(HWND hwnd,
                     const std::wstring& dllName,
                     const char* funcName,
                     const void* params,
                     size_t paramsSize);

} // namespace inject
