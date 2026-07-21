// mythware_control.cpp - 学生机房管理助手控制实现
#include "core/mythware_control.h"
#include "utils/log.h"
#include <cmath>
#include <fstream>
#include <string>

namespace mctl {

// ---------------------------------------------------------------------------
// 已知的学生机房管理助手（ZM软件工作室）固定进程名
// 去掉 .exe 后缀比较
// ---------------------------------------------------------------------------
const std::vector<std::wstring> CLASSROOM_PROCESS_NAMES = {
    L"zmserv",
    L"prozs",
    L"przs",
    L"jfglzs",
    L"jfglzsp",
    L"jfglzsn",
};

// ---------------------------------------------------------------------------
// VB 随机数引擎（用于生成 v9.x ~ v12.x 的动态进程名）
// ---------------------------------------------------------------------------
struct VBRandom {
    long long m_rndSeed = 327680;

    void Randomize(double seed) {
        // VB Randomize 用 Timer 作为种子，这里用 month*day
        m_rndSeed = (long long)(seed * 1000.0) & 0x7FFFFFFF;
        if (m_rndSeed == 0) m_rndSeed = 1;
    }

    float Rnd() {
        // VB 的线性同余生成器
        // 常量：c = 1140671485, a = 12820163, m = 2^31
        m_rndSeed = (m_rndSeed * 1140671485LL + 12820163LL) & 0x7FFFFFFF;
        return (float)((double)m_rndSeed / (double)0x80000000);
    }
};

// 生成所有可能的动态进程名（遍历 month x day 组合）
// 算法：
//   VBMath.m_rndSeed = 327680;
//   VBMath.Randomize(double(month * day));
//   long long n = round(double(VBMath.Rnd()) * 300000.0 + 1.0);
//   name[i] = char(n % 10 + 107);  // 107='k'，生成 k~t 之间的字符
// 返回不带 .exe 后缀的名称列表
static std::vector<std::wstring> GenerateDynamicProcessNames()
{
    std::vector<std::wstring> names;
    for (int month = 1; month <= 12; month++) {
        for (int day = 1; day <= 31; day++) {
            VBRandom rnd;
            rnd.Randomize((double)(month * day));
            long long n = (long long)std::round((double)rnd.Rnd() * 300000.0 + 1.0);
            wchar_t name[6] = {0};
            for (int i = 4; i >= 0; i--) {
                name[i] = (wchar_t)(n % 10LL + 107LL);   // 107 = 'k'
                n /= 10LL;
            }
            std::wstring wname(name);
            // 简单去重
            bool dup = false;
            for (const auto& existing : names) {
                if (existing == wname) { dup = true; break; }
            }
            if (!dup) names.push_back(wname);
        }
    }
    return names;
}

// 内部：通过进程名查找并杀掉（基本逻辑保持不变）
static int KillProcessByName(const std::wstring& name, std::vector<std::wstring>& killed)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    int count = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            // 去掉 .exe 后缀比较
            std::wstring exeName = pe.szExeFile;
            std::wstring baseName = exeName;
            size_t dot = baseName.find_last_of(L'.');
            if (dot != std::wstring::npos) baseName = baseName.substr(0, dot);

            if (_wcsicmp(baseName.c_str(), name.c_str()) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    if (TerminateProcess(hProcess, 1)) {
                        count++;
                        killed.push_back(exeName);
                        logger::Info(L"已杀掉 " + exeName + L" PID=" + WSTR(pe.th32ProcessID));
                    }
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return count;
}

// 内部：判断路径是否位于 C:\Program Files 下（大小写不敏感）
static bool IsUnderProgramFiles(const std::wstring& path)
{
    static const wchar_t* prefix = L"c:\\program files";
    const size_t prefixLen = wcslen(prefix);
    if (path.length() < prefixLen) return false;
    return _wcsnicmp(path.c_str(), prefix, prefixLen) == 0;
}

// 内部：扫描回退
// 当按名称找不到时，扫描所有进程：
//   - 进程名长度 >= 6（5 字符 + .exe）
//   - 去掉 .exe 后所有字符在 'f'(102) ~ 'v'(118) 之间
//   - 排除 smss.exe / sihost.exe / spoolsv.exe
//   - 路径不在 C:\Program Files 下
static int ScanAndKillDynamicProcesses(std::vector<std::wstring>& killed)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    int count = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring exeName = pe.szExeFile;

            // 排除系统进程
            if (_wcsicmp(exeName.c_str(), L"smss.exe") == 0 ||
                _wcsicmp(exeName.c_str(), L"sihost.exe") == 0 ||
                _wcsicmp(exeName.c_str(), L"spoolsv.exe") == 0) {
                continue;
            }

            // 长度检查（5 字符 + .exe = 9，至少 6）
            if (exeName.length() < 6) continue;

            // 去掉 .exe 后缀
            size_t dot = exeName.find_last_of(L'.');
            if (dot == std::wstring::npos) continue;
            std::wstring ext = exeName.substr(dot);
            if (_wcsicmp(ext.c_str(), L".exe") != 0) continue;
            std::wstring baseName = exeName.substr(0, dot);
            if (baseName.empty()) continue;

            // 所有字符必须在 'f'(102) ~ 'v'(118) 之间
            bool allInRange = true;
            for (wchar_t c : baseName) {
                if (c < L'f' || c > L'v') {
                    allInRange = false;
                    break;
                }
            }
            if (!allInRange) continue;

            // 获取进程完整路径，排除 C:\Program Files 下的进程
            HANDLE hQuery = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            bool skipByPath = false;
            if (hQuery) {
                wchar_t path[MAX_PATH] = {0};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hQuery, 0, path, &size)) {
                    if (IsUnderProgramFiles(std::wstring(path))) {
                        skipByPath = true;
                    }
                }
                CloseHandle(hQuery);
            }
            if (skipByPath) continue;

            HANDLE hTerm = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (hTerm) {
                if (TerminateProcess(hTerm, 1)) {
                    count++;
                    killed.push_back(exeName);
                    logger::Info(L"扫描杀掉 " + exeName + L" PID=" + WSTR(pe.th32ProcessID));
                }
                CloseHandle(hTerm);
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return count;
}

// 内部：停止并删除服务
static bool StopAndDeleteService(const std::wstring& serviceName)
{
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        logger::Warn(L"OpenSCManager 失败: " + WSTR(GetLastError()));
        return false;
    }

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(),
                                  SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        // 服务不存在视为成功
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            logger::Info(L"服务 " + serviceName + L" 不存在，跳过");
            return true;
        }
        logger::Warn(L"OpenService " + serviceName + L" 失败: " + WSTR(err));
        return false;
    }

    // 先 ControlService(SERVICE_CONTROL_STOP) 停止
    SERVICE_STATUS ss = {};
    if (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss)) {
        // 等待停止完成
        for (int i = 0; i < 10; i++) {
            if (QueryServiceStatus(hSvc, &ss) && ss.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
            Sleep(200);
        }
        logger::Info(L"已停止服务 " + serviceName);
    }

    // 再 DeleteService 删除
    bool ok = DeleteService(hSvc) != 0;
    if (!ok) {
        logger::Warn(L"删除服务 " + serviceName + L" 失败: " + WSTR(GetLastError()));
    } else {
        logger::Info(L"已删除服务 " + serviceName);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok;
}

KillResult KillClassroomHelper()
{
    KillResult result = {};

    // 重要：必须先停止服务，再杀进程！
    // 否则服务监控会自动重启进程

    // 1. 先停止并删除 zmserv 服务（防止服务重启进程）
    StopAndDeleteService(L"zmserv");

    // 等待服务完全停止
    Sleep(500);

    // 2. 再杀固定进程名（守护进程和主进程）
    for (const auto& name : CLASSROOM_PROCESS_NAMES) {
        result.killedCount += KillProcessByName(name, result.killedNames);
    }

    // 3. 生成动态进程名并杀掉（v9.x ~ v12.x）
    auto dynamicNames = GenerateDynamicProcessNames();
    for (const auto& name : dynamicNames) {
        result.killedCount += KillProcessByName(name, result.killedNames);
    }

    // 4. 扫描回退
    result.killedCount += ScanAndKillDynamicProcesses(result.killedNames);

    logger::Info(L"共杀掉 " + WSTR(result.killedCount) + L" 个助手进程");
    return result;
}

// ---------------------------------------------------------------------------
// 注册表操作辅助函数
// 所有操作带 KEY_WOW64_32KEY 标志（32 位视图），因为学生机房管理助手是 32 位程序
// ---------------------------------------------------------------------------

// 置0注册表 DWORD 值（键不存在视为已解禁）
static bool ClearRegValue(HKEY root, const std::wstring& path, const std::wstring& value)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS) {
        // 键不存在视为已解禁
        return true;
    }
    DWORD zero = 0;
    LONG r = RegSetValueExW(hKey, value.c_str(), 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

// 删除注册表值
static bool DeleteRegValue(HKEY root, const std::wstring& path, const std::wstring& value)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS) {
        // 键不存在视为已删除
        return true;
    }
    LONG r = RegDeleteValueW(hKey, value.c_str());
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

// 设置注册表 DWORD 值
static bool SetRegDword(HKEY root, const std::wstring& path, const std::wstring& value, DWORD data)
{
    HKEY hKey = nullptr;
    DWORD disp = 0;
    LONG r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0,
                             KEY_SET_VALUE | KEY_WOW64_32KEY, nullptr, &hKey, &disp);
    if (r != ERROR_SUCCESS) return false;
    r = RegSetValueExW(hKey, value.c_str(), 0, REG_DWORD, (BYTE*)&data, sizeof(data));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

// 删除注册表键
static bool DeleteRegKey(HKEY root, const std::wstring& path)
{
    LONG r = RegDeleteTreeW(root, path.c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

// 清理 IFEO debugger 劫持
// 删除 HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\<exe>\debugger
static void ClearIFEO()
{
    const std::wstring ifeoBase =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    const std::vector<std::wstring> targets = {
        L"taskkill.exe",    L"ntsd.exe",         L"tasklist.exe",
        L"sethc.exe",       L"sidebar.exe",      L"Chess.exe",
        L"FreeCell.exe",    L"Hearts.exe",       L"Minesweeper.exe",
        L"PurblePlace.exe", L"Mahjong.exe",      L"SpiderSolitaire.exe",
        L"bckgzm.exe",      L"chkrzm.exe",       L"shvlzm.exe",
        L"Solitaire.exe",   L"winmine.exe",      L"Magnify.exe",
        L"QQPCTray.exe",
    };

    for (const auto& exe : targets) {
        std::wstring path = ifeoBase + exe;
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                          KEY_SET_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"debugger");
            RegCloseKey(hKey);
        }
    }
    logger::Info(L"已清理 IFEO debugger 劫持");
}

// 清理 hosts 文件：删除所有以 "127.0.0.1" 开头的行
static bool CleanHostsFile()
{
    wchar_t sysDir[MAX_PATH] = {};
    GetWindowsDirectoryW(sysDir, MAX_PATH);
    std::wstring hostsPathW = std::wstring(sysDir) + L"\\System32\\drivers\\etc\\hosts";
    // 转换为 ANSI 路径（因为用 std::ifstream）
    char hostsPath[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_ACP, 0, hostsPathW.c_str(), -1, hostsPath, sizeof(hostsPath), nullptr, nullptr);
    std::string hostsPathStr(hostsPath);

    // 读取
    std::ifstream fin(hostsPathStr);
    if (!fin.is_open()) {
        logger::Warn(L"打开 hosts 文件失败");
        return false;
    }
    std::vector<std::string> keep;
    std::string line;
    while (std::getline(fin, line)) {
        // 去掉行首空白
        size_t p = line.find_first_not_of(" \t");
        std::string trimmed = (p == std::string::npos) ? line : line.substr(p);
        if (trimmed.find("127.0.0.1") == 0) {
            continue;  // 跳过 127.0.0.1 开头的行
        }
        keep.push_back(line);
    }
    fin.close();

    // 写回
    std::ofstream fout(hostsPathStr, std::ios::trunc);
    if (!fout.is_open()) {
        logger::Warn(L"写回 hosts 文件失败");
        return false;
    }
    for (const auto& l : keep) {
        fout << l << "\n";
    }
    fout.close();
    logger::Info(L"已清理 hosts 文件");
    return true;
}

UnblockSysResult UnblockSystemPrograms()
{
    UnblockSysResult r = {};

    // ========== A. 置 0 的注册表值（HKCU 下）==========
    // CMD
    r.cmd = ClearRegValue(HKEY_CURRENT_USER,
                          L"Software\\Policies\\Microsoft\\Windows\\System",
                          L"DisableCMD");
    // 注册表编辑器
    r.regedit = ClearRegValue(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                              L"DisableRegistryTools");
    // 任务管理器
    r.taskmgr = ClearRegValue(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                              L"DisableTaskMgr");
    // 锁定
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                  L"DisableLockWorkstation");
    // 修改密码
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                  L"DisableChangePassword");
    // 切换用户
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                  L"DisableSwitchUserOption");
    // Win+R 运行
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"NoRun");
    // 限制程序运行
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"RestrictRun");
    // 注销
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"NoLogOff");
    // 开始菜单注销
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"StartMenuLogOff");
    // 托盘右键
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"NoTrayContextMenu");
    // 显示隐藏文件
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"Hidden");
    // 文件夹选项
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                  L"NoFolderOptions");
    // MMC
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Policies\\Microsoft\\MMC",
                  L"RestrictToPermittedSnapins");
    // IE 下载（1803）
    r.explorerDownloads = ClearRegValue(HKEY_CURRENT_USER,
                                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Zones\\3",
                                        L"1803");
    // IE ActiveX（2200）
    ClearRegValue(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Zones\\3",
                  L"2200");

    // ========== B. 删除的注册表值 ==========
    // HKLM - Chrome
    r.chromeDownloads = DeleteRegValue(HKEY_LOCAL_MACHINE,
                                       L"SOFTWARE\\Policies\\Google\\Chrome",
                                       L"DownloadRestrictions");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Google\\Chrome",
                   L"SaveAs");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Google\\Chrome",
                   L"DeveloperToolsAvailability");
    // HKLM - Edge
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Microsoft\\Edge",
                   L"DownloadRestrictions");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Microsoft\\Edge",
                   L"SaveAs");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Microsoft\\Edge",
                   L"DeveloperToolsAvailability");
    // HKLM - Firefox
    r.firefoxDownloads = DeleteRegValue(HKEY_LOCAL_MACHINE,
                                        L"SOFTWARE\\Policies\\Mozilla\\Firefox",
                                        L"DisableDownloads");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Mozilla\\Firefox",
                   L"BlockAboutDownloads");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Mozilla\\Firefox",
                   L"DeveloperToolsAvailability");
    // HKLM - 其他
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                   L"HideFastUserSwitching");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Policies\\Microsoft\\WindowsStore",
                   L"RemoveWindowsStore");
    DeleteRegValue(HKEY_LOCAL_MACHINE,
                   L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout",
                   L"Scancode Map");
    // HKCU
    DeleteRegValue(HKEY_CURRENT_USER,
                   L"Software\\Policies\\Microsoft\\Internet Explorer\\Restrictions",
                   L"NoBrowserSaveAs");
    DeleteRegValue(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                   L"ShowTaskViewButton");

    // ========== C. USB 存储驱动（Start = 3）==========
    r.usbDriver = SetRegDword(HKEY_LOCAL_MACHINE,
                              L"SYSTEM\\CurrentControlSet\\Services\\usbstor",
                              L"Start", 3);
    SetRegDword(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\ControlSet001\\Services\\usbstor",
                L"Start", 3);
    SetRegDword(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\ControlSet002\\Services\\usbstor",
                L"Start", 3);
    SetRegDword(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\ControlSet003\\Services\\usbstor",
                L"Start", 3);

    // ========== D. IFEO debugger 清理 ==========
    ClearIFEO();
    r.ifeo = true;

    // ========== E. hosts 文件清理 ==========
    r.hosts = CleanHostsFile();

    // 构造详细结果
    r.detail = L"CMD: " + std::wstring(r.cmd ? L"已解禁" : L"失败") +
               L", 注册表: " + std::wstring(r.regedit ? L"已解禁" : L"失败") +
               L", 任务管理器: " + std::wstring(r.taskmgr ? L"已解禁" : L"失败") +
               L", IE下载: " + std::wstring(r.explorerDownloads ? L"已解禁" : L"失败") +
               L", Chrome下载: " + std::wstring(r.chromeDownloads ? L"已解禁" : L"失败") +
               L", Firefox下载: " + std::wstring(r.firefoxDownloads ? L"已解禁" : L"失败") +
               L", USB: " + std::wstring(r.usbDriver ? L"已解禁" : L"失败") +
               L", IFEO: " + std::wstring(r.ifeo ? L"已清理" : L"失败") +
               L", hosts: " + std::wstring(r.hosts ? L"已清理" : L"失败");
    logger::Info(L"一键解禁系统程序: " + r.detail);
    return r;
}

bool RestartExplorer()
{
    // 先杀 explorer
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess) {
                        // 退出码 2（参考 MythwareToolkit）
                        TerminateProcess(hProcess, 2);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    Sleep(500);

    // 重新启动 explorer
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"explorer.exe";
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        logger::Info(L"已重启 explorer.exe");
        return true;
    }
    logger::Error(L"重启 explorer 失败: " + WSTR(GetLastError()));
    return false;
}

} // namespace mctl
