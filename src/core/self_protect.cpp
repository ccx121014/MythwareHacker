#include "core/self_protect.h"
#include "utils/log.h"

namespace spctl {

static bool g_selfProtectEnabled = false;
static HANDLE g_hMonitorThread = nullptr;
static bool g_monitorRunning = false;

bool GrantSelfDebugPrivilege()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        logger::Error(L"OpenProcessToken 失败: " + WSTR(GetLastError()));
        return false;
    }

    LUID luid = {};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        logger::Error(L"LookupPrivilegeValue 失败: " + WSTR(GetLastError()));
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);

    if (!ok || GetLastError() != ERROR_SUCCESS) {
        logger::Error(L"AdjustTokenPrivileges 失败: " + WSTR(GetLastError()));
        return false;
    }

    logger::Info(L"已获取 SE_DEBUG_NAME 权限");
    return true;
}

bool SetAutoStart(bool enable)
{
    HKEY hKey = nullptr;
    const wchar_t* regPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ | KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS) {
        logger::Error(L"RegOpenKeyEx 失败: " + WSTR(result));
        return false;
    }

    if (enable) {
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
            logger::Error(L"GetModuleFileName 失败");
            RegCloseKey(hKey);
            return false;
        }

        result = RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)exePath,
                                (DWORD)(wcslen(exePath) + 1) * sizeof(wchar_t));
        if (result != ERROR_SUCCESS) {
            logger::Error(L"RegSetValueEx 失败: " + WSTR(result));
            RegCloseKey(hKey);
            return false;
        }
        logger::Info(L"已设置开机自启动");
    } else {
        result = RegDeleteValueW(hKey, APP_NAME);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            logger::Error(L"RegDeleteValue 失败: " + WSTR(result));
            RegCloseKey(hKey);
            return false;
        }
        logger::Info(L"已取消开机自启动");
    }

    RegCloseKey(hKey);
    return true;
}

bool IsAutoStartEnabled()
{
    HKEY hKey = nullptr;
    const wchar_t* regPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH] = {};
    DWORD size = sizeof(value);
    result = RegQueryValueExW(hKey, APP_NAME, nullptr, nullptr, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

static DWORD WINAPI MonitorThreadProc(LPVOID lpParameter)
{
    DWORD parentPid = GetCurrentProcessId();
    logger::Info(L"进程守护线程已启动 PID=" + WSTR(parentPid));

    while (g_monitorRunning) {
        HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (hProcess) {
            DWORD result = WaitForSingleObject(hProcess, 5000);
            if (result == WAIT_OBJECT_0) {
                logger::Info(L"检测到父进程终止，准备重启...");
                wchar_t exePath[MAX_PATH] = {};
                if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                    STARTUPINFOW si = {};
                    si.cb = sizeof(si);
                    PROCESS_INFORMATION pi = {};
                    if (CreateProcessW(nullptr, exePath, nullptr, nullptr, FALSE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        logger::Info(L"已重启进程 PID=" + WSTR(pi.dwProcessId));
                        CloseHandle(pi.hThread);
                        CloseHandle(pi.hProcess);
                    }
                }
                CloseHandle(hProcess);
                break;
            }
            CloseHandle(hProcess);
        }
        Sleep(2000);
    }
    return 0;
}

bool EnableSelfProtect()
{
    if (g_selfProtectEnabled) return true;

    GrantSelfDebugPrivilege();

    g_monitorRunning = true;
    g_hMonitorThread = CreateThread(nullptr, 0, MonitorThreadProc, nullptr, 0, nullptr);
    if (!g_hMonitorThread) {
        logger::Error(L"创建守护线程失败: " + WSTR(GetLastError()));
        return false;
    }

    g_selfProtectEnabled = true;
    logger::Info(L"防杀保护已启用");
    return true;
}

bool DisableSelfProtect()
{
    if (!g_selfProtectEnabled) return true;

    g_monitorRunning = false;
    if (g_hMonitorThread) {
        WaitForSingleObject(g_hMonitorThread, 2000);
        CloseHandle(g_hMonitorThread);
        g_hMonitorThread = nullptr;
    }

    g_selfProtectEnabled = false;
    logger::Info(L"防杀保护已禁用");
    return true;
}

bool IsSelfProtectEnabled()
{
    return g_selfProtectEnabled;
}

} // namespace spctl