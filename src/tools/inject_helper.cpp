// inject_helper.cpp - 32位注入辅助程序
//
// 当64位主程序需要注入32位进程时，启动本程序完成跨位数注入。
// 用法: inject_helper.exe <pid> <hwnd> <affinity>
//   pid       目标进程ID
//   hwnd      目标窗口句柄（十进制）
//   affinity  WDA亲和值（0=WDA_NONE, 0x11=WDA_EXCLUDEFROMCAPTURE）
//
// 输出（stdout）:
//   OK <exitCode>    成功
//   FAIL <message>   失败
//
// 编译: i686-w64-mingw32-g++ -O2 -s -o inject_helper_x86.exe inject_helper.cpp -static
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>

// 远程参数结构（必须与 hide_hook.cpp 中的 RemoteParams 一致）
#pragma pack(push, 1)
struct RemoteParams {
    HWND hwnd;
    DWORD affinity;
};
#pragma pack(pop)

// 获取自身所在目录
static std::wstring GetSelfDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return std::wstring(path);
}

int main(int argc, char* argv[])
{
    if (argc < 4) {
        printf("FAIL missing arguments\n");
        return 1;
    }

    DWORD pid = (DWORD)atoi(argv[1]);
    HWND hwnd = (HWND)(INT_PTR)atoi(argv[2]);
    DWORD affinity = (DWORD)strtoul(argv[3], nullptr, 0);

    if (pid == 0 || hwnd == nullptr) {
        printf("FAIL invalid pid or hwnd\n");
        return 1;
    }

    // 打开目标进程
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        printf("FAIL OpenProcess error=%lu\n", GetLastError());
        return 1;
    }

    // 获取32位DLL路径
    std::wstring dllPath = GetSelfDir() + L"MythwareHideHook_x86.dll";

    // 第一步：在目标进程分配内存写入DLL路径
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        printf("FAIL VirtualAllocEx error=%lu\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }

    SIZE_T written = 0;
    WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), pathBytes, &written);

    // 远程调用 LoadLibraryW
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    auto pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        pfnLoadLibrary, pRemoteMem, 0, nullptr);
    if (!hThread) {
        printf("FAIL CreateRemoteThread(LoadLibrary) error=%lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);

    // 第二步：本地加载DLL获取导出函数偏移
    HMODULE hLocalDll = LoadLibraryW(dllPath.c_str());
    if (!hLocalDll) {
        printf("FAIL LoadLibrary local error=%lu path=%ls\n", GetLastError(), dllPath.c_str());
        CloseHandle(hProcess);
        return 1;
    }

    FARPROC pfnLocal = GetProcAddress(hLocalDll, "RemoteSetAffinity");
    if (!pfnLocal) {
        printf("FAIL GetProcAddress RemoteSetAffinity\n");
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return 1;
    }

    DWORD_PTR offset = (DWORD_PTR)pfnLocal - (DWORD_PTR)hLocalDll;

    // 第三步：在目标进程中找DLL基址
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        printf("FAIL CreateToolhelp32Snapshot error=%lu\n", GetLastError());
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return 1;
    }

    DWORD_PTR remoteBase = 0;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    // 匹配名：MythwareHideHook_x86（去掉路径和.dll）
    const wchar_t* matchName = L"MythwareHideHook_x86";

    if (Module32FirstW(hSnap, &me)) {
        do {
            std::wstring modName = me.szModule;
            size_t mp = modName.find_last_of(L'.');
            if (mp != std::wstring::npos) modName = modName.substr(0, mp);
            if (_wcsicmp(modName.c_str(), matchName) == 0) {
                remoteBase = (DWORD_PTR)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);

    if (remoteBase == 0) {
        printf("FAIL DLL module not found in target\n");
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return 1;
    }

    DWORD_PTR remoteProc = remoteBase + offset;

    // 第四步：分配参数内存并写入
    RemoteParams params = { hwnd, affinity };
    LPVOID pRemoteParams = VirtualAllocEx(hProcess, nullptr, sizeof(params),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteParams) {
        printf("FAIL VirtualAllocEx(params) error=%lu\n", GetLastError());
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return 1;
    }
    WriteProcessMemory(hProcess, pRemoteParams, &params, sizeof(params), &written);

    // 第五步：远程调用 RemoteSetAffinity
    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)remoteProc, pRemoteParams, 0, nullptr);
    if (!hThread) {
        printf("FAIL CreateRemoteThread(func) error=%lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
        FreeLibrary(hLocalDll);
        CloseHandle(hProcess);
        return 1;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
    FreeLibrary(hLocalDll);
    CloseHandle(hProcess);

    printf("OK %lu\n", exitCode);
    return 0;
}
