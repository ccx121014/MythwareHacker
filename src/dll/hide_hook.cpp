// hide_hook.cpp
// 注入到目标进程的 DLL
// 在目标进程上下文中调用 SetWindowDisplayAffinity
//
// 编译：g++ -DBUILD_DLL -shared -o MythwareHideHook.dll hide_hook.cpp -O2 -s -luser32

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef BOOL (WINAPI *SetWindowDisplayAffinity_t)(HWND, DWORD);

// 远程调用参数结构体（必须和主程序中的 RemoteParams 保持一致）
struct RemoteAffinityParams {
    HWND hwnd;
    DWORD affinity;
};

// 导出函数：接收 LPVOID 参数（CreateRemoteThread 要求）
// 在目标进程上下文中调用 SetWindowDisplayAffinity
// 返回值：0=成功, 1=空参数, 2=WDA调用失败, 3=找不到API, 4=user32加载失败
extern "C" __declspec(dllexport) DWORD WINAPI RemoteSetAffinity(LPVOID lpParam)
{
    RemoteAffinityParams* params = (RemoteAffinityParams*)lpParam;
    if (!params) return 1;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
        if (!hUser32) return 4;
    }

    auto pfn = (SetWindowDisplayAffinity_t)GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    if (!pfn) return 3;

    BOOL ok = pfn(params->hwnd, params->affinity);
    return ok ? 0 : 2;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)hModule;
    (void)lpReserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
