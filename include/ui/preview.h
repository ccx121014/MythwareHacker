// preview.h - 截图预览窗口（模拟教师端视角）
//
// 用 PrintWindow 逐窗口抓图，按 Z 序合成缩略图
// 验证 WDA/Cloak 隐蔽效果
#pragma once
#include "common.h"

namespace preview {

// 注册窗口类
ATOM RegisterClass(HINSTANCE hInst);

// 切换预览窗口显示/隐藏
void Toggle();

// 更新预览位图
void UpdateBitmap();

// 清理资源
void Cleanup();

} // namespace preview
