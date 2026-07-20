// inject_helper.cpp - 32位注入辅助程序
//
// 当64位主程序需要注入32位进程时，启动本程序完成跨位数注入。
// 用法: inject_helper.exe <pid> <affinity>
//   pid       目标进程ID
//   affinity  WDA亲和值（0=WDA_NONE, 0x11=WDA_EXCLUDEFROMCAPTURE）
//
// 输出（stdout）:
//   OK <exitCode> <hwndCount>    成功
//   FAIL <message>               失败
//
// 编译: i686-w64-mingw32-g++ -O2 -s -o inject_helper_x86.exe inject_helper.cpp -static
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct RemoteParams {
    HWND hwnd;
    DWORD affinity;
};
#pragma pack(pop)

static DWORD g_targetPid = 0;
static std::vector<HWND> g_hwndList;

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == g_targetPid && IsWindowVisible(hwnd)) {
        g_hwndList.push_back(hwnd);
    }
    return TRUE;
}

static std::wstring GetSelfDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return std::wstring(path);
}

static DWORD InjectAndSetAffinity(HANDLE hProcess, HWND hwnd, DWORD affinity, const std::wstring& dllPath)
{
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) return 100 + GetLastError();

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), pathBytes, &written) ||
        written != pathBytes) {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        return 110 + GetLastError();
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    auto pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        pfnLoadLibrary, pRemoteMem, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        return 200 + GetLastError();
    }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);

    HMODULE hLocalDll = LoadLibraryW(dllPath.c_str());
    if (!hLocalDll) return 300 + GetLastError();

    FARPROC pfnLocal = GetProcAddress(hLocalDll, "RemoteSetAffinity");
    if (!pfnLocal) {
        FreeLibrary(hLocalDll);
        return 310;
    }

    DWORD_PTR offset = (DWORD_PTR)pfnLocal - (DWORD_PTR)hLocalDll;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, g_targetPid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        FreeLibrary(hLocalDll);
        return 400 + GetLastError();
    }

    DWORD_PTR remoteBase = 0;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

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
        FreeLibrary(hLocalDll);
        return 410;
    }

    DWORD_PTR remoteProc = remoteBase + offset;

    RemoteParams params = { hwnd, affinity };
    LPVOID pRemoteParams = VirtualAllocEx(hProcess, nullptr, sizeof(params),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteParams) {
        FreeLibrary(hLocalDll);
        return 500 + GetLastError();
    }
    if (!WriteProcessMemory(hProcess, pRemoteParams, &params, sizeof(params), &written) ||
        written != sizeof(params)) {
        VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
        FreeLibrary(hLocalDll);
        return 510 + GetLastError();
    }

    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)remoteProc, pRemoteParams, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
        FreeLibrary(hLocalDll);
        return 600 + GetLastError();
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
    FreeLibrary(hLocalDll);

    return exitCode;
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        printf("FAIL missing arguments\n");
        return 1;
    }

    g_targetPid = (DWORD)atoi(argv[1]);
    DWORD affinity = (DWORD)strtoul(argv[2], nullptr, 0);

    if (g_targetPid == 0) {
        printf("FAIL invalid pid\n");
        return 1;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, g_targetPid);
    if (!hProcess) {
        printf("FAIL OpenProcess error=%lu\n", GetLastError());
        return 1;
    }

    std::wstring dllPath = GetSelfDir() + L"MythwareHideHook_x86.dll";

    EnumWindows(EnumWindowsCallback, 0);

    if (g_hwndList.empty()) {
        printf("FAIL no windows found in target process\n");
        CloseHandle(hProcess);
        return 1;
    }

    DWORD totalCount = (DWORD)g_hwndList.size();
    DWORD successCount = 0;
    DWORD lastExitCode = 0;

    for (size_t i = 0; i < g_hwndList.size(); i++) {
        DWORD code = InjectAndSetAffinity(hProcess, g_hwndList[i], affinity, dllPath);
        if (code == 0) successCount++;
        lastExitCode = code;
    }

    CloseHandle(hProcess);

    printf("OK %lu %lu\n", lastExitCode, successCount);
    return 0;
}
