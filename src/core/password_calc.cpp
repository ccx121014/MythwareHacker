// password_calc.cpp - 动态密码计算器实现
#include "core/password_calc.h"
#include "utils/log.h"
#include <string>

namespace pwcalc {

// 宽字符串转ANSI字符串（匹配原版 GetComputerNameA 的行为）
static std::string WideToAnsi(const std::wstring& w)
{
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                        &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring ToBase7(unsigned long long n)
{
    if (n == 0) return L"0";
    std::wstring result;
    while (n > 0) {
        result = std::wstring(1, L'0' + (wchar_t)(n % 7)) + result;
        n /= 7;
    }
    return result;
}

std::wstring Calculate(AlgoVersion version, int year, int month, int day,
                       const std::wstring& computerName)
{
    long long result = 0;
    std::wstring prefix;

    switch (version) {
    case AlgoVersion::PreV10:
        // 首位为 8，后面为 16 × (年×91 + 月×13 + 日×57)
        result = 16LL * (year * 91 + month * 13 + day * 57);
        prefix = L"8";
        break;

    case AlgoVersion::V10ToV11:
        // 上面结果 +11
        result = 16LL * (year * 91 + month * 13 + day * 57) + 11;
        prefix = L"8";
        break;

    case AlgoVersion::V11ToV1106:
        // 年×789 + 月×123 + 日×456 + 111
        result = (long long)year * 789 + (long long)month * 123 + (long long)day * 456 + 111;
        return std::to_wstring(result);

    case AlgoVersion::V1106ToV12: {
        // (月×159 + 日×357 + 计算机名末位 ASCII × 258) 转 7 进制
        // 注意：原版用 char（ANSI字节），不是 wchar_t
        // 计算机名通常只含ASCII，但为严格匹配原版行为，取最后一个字节
        char lastChar = '0';
        if (!computerName.empty()) {
            // 转为ANSI取末字节，匹配原版 char lastChar = szComputerName[len-1]
            std::string ansi = WideToAnsi(computerName);
            if (!ansi.empty()) lastChar = ansi.back();
        }
        int ascii = (int)(unsigned char)lastChar;
        result = (long long)month * 159 + (long long)day * 357 + (long long)ascii * 258;
        return ToBase7((unsigned long long)result);
    }
    }

    return prefix + std::to_wstring(result);
}

// 解析版本号字符串为 (major, minor)
static bool ParseVersion(const std::wstring& v, int& major, int& minor)
{
    major = 0;
    minor = 0;
    size_t dot = v.find(L'.');
    if (dot == std::wstring::npos) {
        // 没有点，整体当主版本
        for (wchar_t c : v) {
            if (c < L'0' || c > L'9') return false;
            major = major * 10 + (c - L'0');
        }
        return true;
    }
    // 主版本
    for (size_t i = 0; i < dot; i++) {
        if (v[i] < L'0' || v[i] > L'9') return false;
        major = major * 10 + (v[i] - L'0');
    }
    // 次版本（取前两位）
    int cnt = 0;
    for (size_t i = dot + 1; i < v.length() && cnt < 2; i++) {
        if (v[i] < L'0' || v[i] > L'9') break;
        minor = minor * 10 + (v[i] - L'0');
        cnt++;
    }
    return true;
}

std::wstring CalculateAuto(const std::wstring& versionStr,
                           int year, int month, int day,
                           const std::wstring& computerName)
{
    int major = 0, minor = 0;
    if (!ParseVersion(versionStr, major, minor)) {
        return L"版本号格式错误";
    }

    // v9.x ~ 10.0（原版标签"10.1-"表示10.1之前）
    if (major < 10 || (major == 10 && minor < 1)) {
        return Calculate(AlgoVersion::PreV10, year, month, day, computerName);
    }
    // 10.1 ~ 10.x（原版标签"10.x"）
    if (major == 10) {
        return Calculate(AlgoVersion::V10ToV11, year, month, day, computerName);
    }
    // 11.0 ~ 11.05（原版标签"11.0x"）
    if (major == 11 && minor < 6) {
        return Calculate(AlgoVersion::V11ToV1106, year, month, day, computerName);
    }
    // 11.06 ~ 12.0+（原版标签"11.06~12.0"）
    if ((major == 11 && minor >= 6) || major >= 12) {
        return Calculate(AlgoVersion::V1106ToV12, year, month, day, computerName);
    }
    return L"不支持的版本";
}

AllResults CalculateAll(int year, int month, int day,
                        const std::wstring& computerName)
{
    AllResults r;
    r.preV10     = Calculate(AlgoVersion::PreV10,     year, month, day, computerName);
    r.v10ToV11   = Calculate(AlgoVersion::V10ToV11,   year, month, day, computerName);
    r.v11ToV1106 = Calculate(AlgoVersion::V11ToV1106, year, month, day, computerName);
    r.v1106ToV12 = Calculate(AlgoVersion::V1106ToV12, year, month, day, computerName);
    return r;
}

std::wstring GetLocalComputerName()
{
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    ::GetComputerNameW(name, &size);
    return std::wstring(name);
}

std::wstring ReadMythwarePassword()
{
    // 1. 从注册表读取 Knock1 二进制值
    HKEY hKey = nullptr;
    BYTE data[256] = {};
    DWORD dataSize = sizeof(data);

    // 先尝试 WOW6432Node（64位系统上32位注册表视图）
    LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\TopDomain\\e-Learning Class\\Student",
        0, KEY_READ, &hKey);
    if (r != ERROR_SUCCESS) {
        // 回退到普通路径
        r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\TopDomain\\e-Learning Class\\Student",
            0, KEY_READ, &hKey);
    }
    if (r != ERROR_SUCCESS) return L"";

    r = RegQueryValueExW(hKey, L"Knock1", nullptr, nullptr, data, &dataSize);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS || dataSize < 4) return L"";

    // 2. 解密：每4字节做 XOR
    // MythwareToolkit 方式：PECL 模式
    // retKeyVal[i+0] ^= ('P' ^ 'E')  = 0x50 ^ 0x45
    // retKeyVal[i+1] ^= ('C' ^ 'L')  = 0x43 ^ 0x4c
    // retKeyVal[i+2] ^= ('L' ^ 'C')  = 0x4c ^ 0x43
    // retKeyVal[i+3] ^= ('E' ^ 'P')  = 0x45 ^ 0x50
    BYTE* buf = data;
    for (DWORD i = 0; i + 3 < dataSize; i += 4) {
        buf[i + 0] ^= (0x50 ^ 0x45);
        buf[i + 1] ^= (0x43 ^ 0x4c);
        buf[i + 2] ^= (0x4c ^ 0x43);
        buf[i + 3] ^= (0x45 ^ 0x50);
    }

    // 3. 提取密码字符串
    // 解密后，取每4字节的第0字节拼成ASCII密码
    // （数据是 UTF-16LE，但密码字符在低位字节）
    std::wstring password;
    for (DWORD i = 0; i + 3 < dataSize; i += 4) {
        if (buf[i] == 0) break;
        password += (wchar_t)buf[i];
    }

    return password;
}

} // namespace pwcalc
