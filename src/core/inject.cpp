// inject.cpp - DLL 注入实现
#include "core/inject.h"
#include "utils/log.h"
#include <cstring>

namespace inject {

static std::wstring AsciiToWide(const char* s)
{
    std::wstring r;
    while (*s) r += (wchar_t)(unsigned char)*s++;
    return r;
}

// 内部：检查位数匹配
static bool CheckArchMatch(HANDLE hProcess, std::wstring& err)
{
    bool target64 = common::IsProcess64Bit(hProcess);
    bool self64 = common::IsSelf64Bit();
    if (target64 != self64) {
        err = L"位数不匹配！本程序: " + std::wstring(self64 ? L"64位" : L"32位") +
              L"，目标进程: " + std::wstring(target64 ? L"64位" : L"32位");
        return false;
    }
    return true;
}

// 内部：获取自身所在目录
static std::wstring GetSelfDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return std::wstring(path);
}

// 内部：根据自身位数选择正确的 DLL 文件名后缀
static std::wstring GetArchDllName(const std::wstring& dllName)
{
    // 如果调用方已经传了带 _x64/_x86 的名字，直接用
    if (dllName.find(L"_x64") != std::wstring::npos ||
        dllName.find(L"_x86") != std::wstring::npos) {
        return dllName;
    }
    // 否则自动加上位数后缀
    std::wstring base = dllName;
    std::wstring ext = L".dll";
    size_t extPos = base.find(ext);
    if (extPos != std::wstring::npos) {
        base = base.substr(0, extPos);
    }
    return base + (common::IsSelf64Bit() ? L"_x64.dll" : L"_x86.dll");
}

Result InjectAndCall(HWND hwnd,
                     const std::wstring& dllName,
                     const char* funcName,
                     const void* params,
                     size_t paramsSize)
{
    Result result = {};
    result.success = false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        result.error = L"目标窗口是本进程";
        return result;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        result.error = L"OpenProcess 失败，错误码: " + std::to_wstring(GetLastError());
        return result;
    }

    if (!CheckArchMatch(hProcess, result.error)) {
        CloseHandle(hProcess);
        return result;
    }

    // 获取 DLL 完整路径（自动加位数后缀）
    std::wstring fullDllName = GetArchDllName(dllName);
    std::wstring dllPath = GetSelfDir() + fullDllName;

    // 在目标进程分配内存并写入 DLL 路径
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        result.error = L"VirtualAllocEx(DLL路径) 失败: " + std::to_wstring(GetLastError());
        CloseHandle(hProcess);
        return result;
    }

    SIZE_T bytesWritten = 0;
    WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), pathBytes, &bytesWritten);

    // 第一步：远程调用 LoadLibraryW 加载 DLL
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    auto pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        pfnLoadLibrary, pRemoteMem, 0, nullptr);
    if (!hThread) {
        result.error = L"CreateRemoteThread(LoadLibrary) 失败: " + std::to_wstring(GetLastError()) +
                       L"（可能被 Exploit Protection / AppLocker 拦截）";
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return result;
    }
    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);

    // 第二步：本地加载 DLL 获取导出函数偏移
    HMODULE hLocalDll = LoadLibraryW(dllPath.c_str());
    if (!hLocalDll) {
        result.error = L"本地加载 DLL 失败: " + dllPath + L" 错误: " + std::to_wstring(GetLastError());
        CloseHandle(hProcess);
        return result;
    }

    FARPROC pfnLocal = GetProcAddress(hLocalDll, funcName);
    if (!pfnLocal) {
        result.error = L"找不到导出函数 " + AsciiToWide(funcName);
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return result;
    }

    DWORD_PTR offset = (DWORD_PTR)pfnLocal - (DWORD_PTR)hLocalDll;

    // 第三步：在目标进程中找 DLL 基址
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        result.error = L"CreateToolhelp32Snapshot 失败: " + std::to_wstring(GetLastError());
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return result;
    }

    DWORD_PTR remoteBase = 0;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    // 去掉路径和后缀的 DLL 名，用于匹配
    std::wstring matchName = fullDllName;
    size_t slashPos = matchName.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) matchName = matchName.substr(slashPos + 1);
    size_t dotPos = matchName.find_last_of(L'.');
    if (dotPos != std::wstring::npos) matchName = matchName.substr(0, dotPos);

    if (Module32FirstW(hSnap, &me)) {
        do {
            std::wstring modName = me.szModule;
            size_t mp = modName.find_last_of(L'.');
            if (mp != std::wstring::npos) modName = modName.substr(0, mp);
            if (_wcsicmp(modName.c_str(), matchName.c_str()) == 0) {
                remoteBase = (DWORD_PTR)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);

    if (remoteBase == 0) {
        result.error = L"目标进程中找不到 DLL 模块: " + matchName;
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return result;
    }

    DWORD_PTR remoteProc = remoteBase + offset;

    // 第四步：在目标进程分配参数内存并写入
    LPVOID pRemoteParams = nullptr;
    if (params && paramsSize > 0) {
        pRemoteParams = VirtualAllocEx(hProcess, nullptr, paramsSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteParams) {
            result.error = L"VirtualAllocEx(参数) 失败: " + std::to_wstring(GetLastError());
            FreeLibrary(hLocalDll);
            CloseHandle(hProcess);
            return result;
        }
        WriteProcessMemory(hProcess, pRemoteParams, params, paramsSize, &bytesWritten);
    }

    // 第五步：远程调用导出函数
    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)remoteProc, pRemoteParams, 0, nullptr);
    if (!hThread) {
        result.error = L"CreateRemoteThread(函数调用) 失败: " + std::to_wstring(GetLastError());
        if (pRemoteParams) VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return result;
    }

    WaitForSingleObject(hThread, 3000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    if (pRemoteParams) VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
    FreeLibrary(hLocalDll);
    CloseHandle(hProcess);

    result.success = true;
    result.exitCode = exitCode;

    if (exitCode != 0) {
        result.error = L"远程调用返回错误码: " + std::to_wstring(exitCode) +
                       L"（0=成功, 1=参数错误, 2=WDA失败, 3=找不到API, 4=user32加载失败）";
    }

    logger::Info(L"注入调用 " + fullDllName + L"!" + AsciiToWide(funcName) +
              L" PID=" + std::to_wstring(pid) + L" 退出码=" + std::to_wstring(exitCode));
    return result;
}

} // namespace inject
