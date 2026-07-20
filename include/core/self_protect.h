#pragma once
#include "common.h"

namespace spctl {

bool EnableSelfProtect();
bool DisableSelfProtect();
bool IsSelfProtectEnabled();
bool SetAutoStart(bool enable);
bool IsAutoStartEnabled();
bool GrantSelfDebugPrivilege();

} // namespace spctl