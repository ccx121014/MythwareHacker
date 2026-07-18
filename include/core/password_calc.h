// password_calc.h - 学生机房管理助手动态密码计算器
//
// 覆盖 v9.x ~ v12.0 的 4 套密码算法
//   1. 10.0 前：首位为 8，后面为 16 × (年×91 + 月×13 + 日×57)
//   2. 10.0 ~ 11.0：上面结果 +11
//   3. 11.0 ~ 11.06 首个发布版：年×789 + 月×123 + 日×456 + 111
//   4. 11.06 第三版 ~ 12.0：(月×159 + 日×357 + 计算机名末位 ASCII × 258) 转 7 进制
#pragma once
#include "common.h"

namespace pwcalc {

// 算法版本
enum class AlgoVersion {
    PreV10,         // 10.0 前
    V10ToV11,       // 10.0 ~ 11.0
    V11ToV1106,     // 11.0 ~ 11.06 首发版
    V1106ToV12,     // 11.06 第三版 ~ 12.0
};

// 计算临时密码
//   version: 算法版本
//   year/month/day: 日期
//   computerName: 计算机名（仅 V1106ToV12 用到末位字符）
std::wstring Calculate(AlgoVersion version, int year, int month, int day,
                       const std::wstring& computerName = L"");

// 自动根据版本号字符串选择算法
//   versionStr: 如 "9.5"、"10.2"、"11.06"、"12.0"
std::wstring CalculateAuto(const std::wstring& versionStr,
                           int year, int month, int day,
                           const std::wstring& computerName = L"");

// 获取当前计算机名
std::wstring GetLocalComputerName();

// 内部：十进制转 7 进制字符串
std::wstring ToBase7(unsigned long long n);

// 从注册表读取并解密极域密码（Knock1）
// 返回空字符串表示读取失败
std::wstring ReadMythwarePassword();

} // namespace pwcalc
