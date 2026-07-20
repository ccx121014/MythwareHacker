// persist.cpp - 状态持久化实现
#include "utils/persist.h"
#include "utils/window_utils.h"
#include "utils/log.h"

namespace persist {

std::wstring GetFilePath()
{
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"MythwareHacker_restore.dat";
}

void Save(const std::vector<SavedWindow>& windows)
{
    if (windows.empty()) {
        Delete();
        return;
    }

    HANDLE hFile = CreateFileW(GetFilePath().c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    DWORD count = static_cast<DWORD>(windows.size());
    WriteFile(hFile, &count, sizeof(count), &written, nullptr);

    for (const auto& w : windows) {
        WriteFile(hFile, &w.placement, sizeof(WINDOWPLACEMENT), &written, nullptr);

        DWORD nameLen = static_cast<DWORD>(w.processName.length() * sizeof(wchar_t));
        WriteFile(hFile, &nameLen, sizeof(nameLen), &written, nullptr);
        if (nameLen > 0)
            WriteFile(hFile, w.processName.c_str(), nameLen, &written, nullptr);

        DWORD titleLen = static_cast<DWORD>(w.title.length() * sizeof(wchar_t));
        WriteFile(hFile, &titleLen, sizeof(titleLen), &written, nullptr);
        if (titleLen > 0)
            WriteFile(hFile, w.title.c_str(), titleLen, &written, nullptr);
    }

    CloseHandle(hFile);
}

void Delete()
{
    DeleteFileW(GetFilePath().c_str());
}

int LoadAndRestore()
{
    HANDLE hFile = CreateFileW(GetFilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD read = 0;
    DWORD count = 0;
    if (!ReadFile(hFile, &count, sizeof(count), &read, nullptr) || count == 0) {
        CloseHandle(hFile);
        return 0;
    }

    int restored = 0;
    for (DWORD i = 0; i < count; i++) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        if (!ReadFile(hFile, &wp, sizeof(WINDOWPLACEMENT), &read, nullptr)) break;

        DWORD nameLen = 0;
        if (!ReadFile(hFile, &nameLen, sizeof(nameLen), &read, nullptr)) break;
        if (nameLen > 4096) break;  // 先检查再分配
        std::wstring processName;
        if (nameLen > 0) {
            processName.resize(nameLen / sizeof(wchar_t));
            if (!ReadFile(hFile, &processName[0], nameLen, &read, nullptr)) break;
        }

        DWORD titleLen = 0;
        if (!ReadFile(hFile, &titleLen, sizeof(titleLen), &read, nullptr)) break;
        if (titleLen > 4096) break;  // 先检查再分配
        std::wstring title;
        if (titleLen > 0) {
            title.resize(titleLen / sizeof(wchar_t));
            if (!ReadFile(hFile, &title[0], titleLen, &read, nullptr)) break;
        }

        // 先 FindWindow 精确匹配
        HWND hwnd = wutil::FindWindowByTitle(title);
        if (!hwnd && !processName.empty()) {
            // 遍历按标题+进程名匹配
            auto all = wutil::EnumAllTopLevelWindows();
            for (const auto& w : all) {
                if (w.title == title && _wcsicmp(w.processName.c_str(), processName.c_str()) == 0) {
                    hwnd = w.hwnd;
                    break;
                }
            }
        }

        if (hwnd && IsWindowVisible(hwnd)) {
            SetWindowPlacement(hwnd, &wp);
            InvalidateRect(hwnd, nullptr, TRUE);
            restored++;
        }
    }

    CloseHandle(hFile);
    DeleteFileW(GetFilePath().c_str());

    if (restored > 0) {
        logger::Info(L"恢复 " + WSTR(restored) + L" 个上次未恢复的窗口");
    }
    return restored;
}

} // namespace persist
