// driver_control.h - 驱动卸载模块
//
// 卸载极域的内核驱动以解除限制：
//   - TDFileFilter：U 盘文件过滤驱动
//   - TDNetFilter：网络过滤驱动
//
// 方法：sc stop + sc delete（需要管理员权限）
// 备选：通过 SCM API 编程实现
#pragma once
#include "common.h"

namespace drvctl {

struct DriverInfo {
    std::wstring name;        // 驱动名（如 TDFileFilter）
    std::wstring description;
    bool isRunning;
    bool exists;
};

// 查询驱动状态
DriverInfo QueryDriver(const std::wstring& driverName);

// 停止并删除驱动
//   driverName: 驱动名（不带 .sys 后缀）
bool StopAndDeleteDriver(const std::wstring& driverName);

// 解除极域 U 盘限制（卸载 TDFileFilter）
bool UnblockUSB();

// 解除极域网络限制（卸载 TDNetFilter）
bool UnblockNetwork();

// 完整的网络限制解除（DeviceIoControl + 杀进程 + SCM删除）
bool UnblockNetworkFull();

// 解除键盘锁（向 TDKeybd 设备发控制码）
bool UnblockKeyboard();

// 一次性解除所有限制（U 盘 + 网络）
struct UnblockResult {
    bool usbOk;
    bool netOk;
    std::wstring detail;
};
UnblockResult UnblockAll();

} // namespace drvctl
