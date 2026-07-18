// log.h - 日志系统 + 崩溃诊断
#pragma once
#include "common.h"

namespace logger {

// 初始化日志系统（设置崩溃处理器）
void Init();

// 写运行日志（追加模式）
void Info(const std::wstring& msg);
void Warn(const std::wstring& msg);
void Error(const std::wstring& msg);

// 写崩溃日志（寄存器 + 栈回溯）
void WriteCrash(EXCEPTION_POINTERS* ep);

// 获取日志文件路径
std::wstring GetRunLogPath();
std::wstring GetCrashLogPath();

} // namespace log
