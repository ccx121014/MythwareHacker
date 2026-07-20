// driver_control.cpp - 驱动卸载实现
#include "core/driver_control.h"
#include "utils/log.h"

namespace drvctl {

// 内部辅助：按 EXE 名杀进程
static void KillProcessByName(const wchar_t* exeName)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 1);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

DriverInfo QueryDriver(const std::wstring& driverName)
{
    DriverInfo info = {};
    info.name = driverName;
    info.exists = false;
    info.isRunning = false;

    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return info;

    SC_HANDLE hSvc = OpenServiceW(hScm, driverName.c_str(),
                                  SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!hSvc) {
        CloseServiceHandle(hScm);
        return info;
    }
    info.exists = true;

    SERVICE_STATUS status = {};
    if (QueryServiceStatus(hSvc, &status)) {
        info.isRunning = (status.dwCurrentState == SERVICE_RUNNING);
    }

    // 查询描述
    DWORD bytesNeeded = 0;
    QueryServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &bytesNeeded);
    if (bytesNeeded > 0 && bytesNeeded < 4096) {
        std::vector<BYTE> buf(bytesNeeded);
        if (QueryServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, buf.data(), bytesNeeded, &bytesNeeded)) {
            auto* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(buf.data());
            if (desc->lpDescription) {
                info.description = desc->lpDescription;
            }
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return info;
}

bool StopAndDeleteDriver(const std::wstring& driverName)
{
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) {
        logger::Error(L"OpenSCManager 失败: " + WSTR(GetLastError()));
        return false;
    }

    SC_HANDLE hSvc = OpenServiceW(hScm, driverName.c_str(),
                                  SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        // 驱动不存在，视为已卸载
        CloseServiceHandle(hScm);
        logger::Info(L"驱动 " + driverName + L" 不存在（可能已卸载）");
        return true;
    }

    // 先停止
    SERVICE_STATUS status = {};
    if (QueryServiceStatus(hSvc, &status) && status.dwCurrentState != SERVICE_STOPPED) {
        if (!ControlService(hSvc, SERVICE_CONTROL_STOP, &status)) {
            logger::Warn(L"停止驱动 " + driverName + L" 失败: " + WSTR(GetLastError()));
        }
        // 等待停止
        for (int i = 0; i < 10; i++) {
            if (QueryServiceStatus(hSvc, &status) && status.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(300);
        }
    }

    // 删除服务
    BOOL ok = DeleteService(hSvc);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);

    if (ok) {
        logger::Info(L"已卸载驱动 " + driverName);
    } else {
        logger::Error(L"删除驱动 " + driverName + L" 失败: " + WSTR(GetLastError()));
    }
    return ok != FALSE;
}

bool UnblockUSB()
{
    // 软解禁：通过 FilterConnectCommunicationPort 发送消息
    // 注意：FilterConnectCommunicationPort 在 fltlib.h 中声明，需要链接 fltlib.lib
    // MinGW 可能没有 fltlib.h，用动态加载方式
    typedef HRESULT (WINAPI *FilterConnectCommunicationPort_t)(LPCWSTR, DWORD, LPVOID, DWORD, LPSECURITY_ATTRIBUTES, HANDLE*);
    HMODULE hFltlib = LoadLibraryW(L"fltlib.dll");
    if (hFltlib) {
        auto pfnConnect = (FilterConnectCommunicationPort_t)GetProcAddress(hFltlib, "FilterConnectCommunicationPort");
        if (pfnConnect) {
            HANDLE hPort = nullptr;
            HRESULT hr = pfnConnect(L"\\TDFileFilterPort", 0, nullptr, 0, nullptr, &hPort);
            if (SUCCEEDED(hr) && hPort) {
                // 发送解禁消息
                typedef HRESULT (WINAPI *FilterSendMessage_t)(HANDLE, LPVOID, DWORD, LPVOID, DWORD, LPVOID);
                auto pfnSend = (FilterSendMessage_t)GetProcAddress(hFltlib, "FilterSendMessage");
                if (pfnSend) {
                    DWORD buf[4] = { 8, 0, 0, 0 };
                    pfnSend(hPort, buf, 16, nullptr, 0, nullptr);
                }
                CloseHandle(hPort);
            }
        }
        FreeLibrary(hFltlib);
    }

    // 硬解禁：SCM 停止+删除
    return StopAndDeleteDriver(L"TDFileFilter");
}

bool UnblockNetworkFull()
{
    // 1. 打开 TDNetFilter 设备，发送停止指令
    HANDLE hDevice = CreateFileW(L"\\\\.\\TDNetFilter", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice != INVALID_HANDLE_VALUE) {
        DeviceIoControl(hDevice, 0x120014, nullptr, 0, nullptr, 0, nullptr, nullptr);
        CloseHandle(hDevice);
    }

    // 2. 杀掉相关进程
    KillProcessByName(L"MasterHelper.exe");
    KillProcessByName(L"GATESRV.exe");

    // 3. SCM 停止+删除
    return StopAndDeleteDriver(L"TDNetFilter");
}

bool UnblockNetwork()
{
    return UnblockNetworkFull();
}

bool UnblockKeyboard()
{
    HANDLE hDevice = CreateFileW(L"\\\\.\\TDKeybd", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) {
        logger::Warn(L"无法打开 TDKeybd 设备（可能未安装键盘锁驱动）");
        return false;
    }
    BOOL bEnable = TRUE;
    BOOL ok = DeviceIoControl(hDevice, 0x220000, &bEnable, sizeof(bEnable),
                              nullptr, 0, nullptr, nullptr);
    CloseHandle(hDevice);
    if (ok) {
        logger::Info(L"已解除键盘锁");
    } else {
        logger::Warn(L"解除键盘锁失败: " + WSTR(GetLastError()));
    }
    return ok != FALSE;
}

UnblockResult UnblockAll()
{
    UnblockResult r = {};
    r.usbOk = UnblockUSB();
    r.netOk = UnblockNetwork();
    UnblockKeyboard();  // 新增
    r.detail = L"U盘限制: " + std::wstring(r.usbOk ? L"已解除" : L"失败") +
               L", 网络限制: " + std::wstring(r.netOk ? L"已解除" : L"失败");
    logger::Info(r.detail);
    return r;
}

} // namespace drvctl
