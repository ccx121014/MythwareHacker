// persist.h - 状态持久化（崩溃/退出后重启可恢复窗口）
#pragma once
#include "common.h"

namespace persist {

// 保存单个隐蔽窗口的状态（WINDOWPLACEMENT + 标题 + 进程名）
struct SavedWindow {
    WINDOWPLACEMENT placement;
    std::wstring title;
    std::wstring processName;
};

// 获取持久化文件路径
std::wstring GetFilePath();

// 保存所有隐蔽窗口到文件
void Save(const std::vector<SavedWindow>& windows);

// 从文件加载并尝试恢复窗口（按标题/进程名匹配）
// 返回成功恢复的窗口数
int LoadAndRestore();

// 删除持久化文件
void Delete();

} // namespace persist
