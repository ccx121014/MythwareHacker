// log.cpp - 日志系统实现
#include "utils/log.h"
#include <cstdio>
#include <ctime>
#include <cstring>

namespace logger {

static std::wstring g_logDir;

static std::wstring GetLogDir()
{
    if (!g_logDir.empty()) return g_logDir;
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    g_logDir = std::wstring(tempPath) + L"MythwareHacker\\";
    CreateDirectoryW(g_logDir.c_str(), nullptr);
    return g_logDir;
}

std::wstring GetRunLogPath()
{
    return GetLogDir() + L"MythwareHacker_run.log";
}

std::wstring GetCrashLogPath()
{
    return GetLogDir() + L"MythwareHacker_crash.log";
}

static std::wstring CurrentTimeStr()
{
    time_t now = time(nullptr);
    tm local = {};
#if defined(_WIN32) || defined(__MINGW32__)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec);
    return buf;
}

static void WriteLog(const wchar_t* level, const std::wstring& msg)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, GetRunLogPath().c_str(), L"a, ccs=UTF-8") != 0 || !fp) return;
    fwprintf(fp, L"[%s] [%s] %s\n", CurrentTimeStr().c_str(), level, msg.c_str());
    fclose(fp);
}

void Info(const std::wstring& msg) { WriteLog(L"INFO", msg); }
void Warn(const std::wstring& msg) { WriteLog(L"WARN", msg); }
void Error(const std::wstring& msg) { WriteLog(L"ERROR", msg); }

// 崩溃处理
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    WriteCrash(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

void WriteCrash(EXCEPTION_POINTERS* ep)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, GetCrashLogPath().c_str(), L"a, ccs=UTF-8") != 0 || !fp) return;

    fwprintf(fp, L"========== Crash %s ==========\n", CurrentTimeStr().c_str());
    fwprintf(fp, L"Exception Code: 0x%08X\n", ep->ExceptionRecord->ExceptionCode);
    fwprintf(fp, L"Exception Address: 0x%p\n", ep->ExceptionRecord->ExceptionAddress);

    CONTEXT* c = ep->ContextRecord;
#ifdef _M_X64
    fwprintf(fp, L"\nRegisters:\n");
    fwprintf(fp, L"  RAX: 0x%016llX  RBX: 0x%016llX  RCX: 0x%016llX\n", c->Rax, c->Rbx, c->Rcx);
    fwprintf(fp, L"  RDX: 0x%016llX  RSI: 0x%016llX  RDI: 0x%016llX\n", c->Rdx, c->Rsi, c->Rdi);
    fwprintf(fp, L"  RBP: 0x%016llX  RSP: 0x%016llX  RIP: 0x%016llX\n", c->Rbp, c->Rsp, c->Rip);
    fwprintf(fp, L"  R8 : 0x%016llX  R9 : 0x%016llX  R10: 0x%016llX\n", c->R8, c->R9, c->R10);
    fwprintf(fp, L"  R11: 0x%016llX  R12: 0x%016llX  R13: 0x%016llX\n", c->R11, c->R12, c->R13);
    fwprintf(fp, L"  R14: 0x%016llX  R15: 0x%016llX\n", c->R14, c->R15);
#else
    fwprintf(fp, L"\nRegisters:\n");
    fwprintf(fp, L"  EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X\n", c->Eax, c->Ebx, c->Ecx);
    fwprintf(fp, L"  EDX: 0x%08X  ESI: 0x%08X  EDI: 0x%08X\n", c->Edx, c->Esi, c->Edi);
    fwprintf(fp, L"  EBP: 0x%08X  ESP: 0x%08X  EIP: 0x%08X\n", c->Ebp, c->Esp, c->Eip);
#endif

    // 简单栈回溯（用 IsBadReadPtr 检查可读性，避免 SEH 在 MinGW 上的兼容问题）
    fwprintf(fp, L"\nStack backtrace:\n");
#ifdef _M_X64
    DWORD64* sp = (DWORD64*)c->Rsp;
    for (int i = 0; i < 32; i++) {
        if (IsBadReadPtr(&sp[i], sizeof(DWORD64))) break;
        fwprintf(fp, L"  [RSP+0x%02X] 0x%016llX\n", i * 8, sp[i]);
    }
#else
    DWORD* sp = (DWORD*)c->Esp;
    for (int i = 0; i < 32; i++) {
        if (IsBadReadPtr(&sp[i], sizeof(DWORD))) break;
        fwprintf(fp, L"  [ESP+0x%02X] 0x%08X\n", i * 4, sp[i]);
    }
#endif

    fwprintf(fp, L"==========================================\n\n");
    fclose(fp);
}

void Init()
{
    SetUnhandledExceptionFilter(CrashHandler);
    Info(L"===== MythwareHacker 启动 =====");
    Info(L"系统 Build: " + WSTR(common::GetSystemBuildNumber()) +
         L", 自身位数: " + std::wstring(common::IsSelf64Bit() ? L"64" : L"32") +
         L", WDA支持: " + std::wstring(common::IsWdaExcludeFromCaptureSupported() ? L"是" : L"否"));
}

} // namespace logger
