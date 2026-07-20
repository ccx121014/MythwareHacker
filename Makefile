# Makefile - MythwareHacker
# 集大成应用构建脚本
#
# 依赖：MinGW-w64（gcc/g++）
# 用法：
#   make            默认构建当前架构
#   make ARCH=32    强制 32 位
#   make ARCH=64    强制 64 位
#   make all        同时构建 32 和 64 位
#   make clean

ARCH ?= 64

ifeq ($(ARCH),32)
    CXX      := i686-w64-mingw32-g++
    WINDRES  := i686-w64-mingw32-windres
    ARCH_FLAG := -m32
    ARCH_SUFFIX := x86
else
    CXX      := x86_64-w64-mingw32-g++
    WINDRES  := x86_64-w64-mingw32-windres
    ARCH_FLAG := -m64
    ARCH_SUFFIX := x64
endif

SRC_DIR  := src
DLL_DIR  := src/dll
OBJ_DIR  := bin/obj_$(ARCH)
BIN_DIR  := bin

# 源文件列表（主程序）
MAIN_SRCS := \
    $(SRC_DIR)/main.cpp \
    $(SRC_DIR)/ui/tray.cpp \
    $(SRC_DIR)/ui/float_window.cpp \
    $(SRC_DIR)/ui/preview.cpp \
    $(SRC_DIR)/ui/hotkey.cpp \
    $(SRC_DIR)/ui/menu.cpp \
    $(SRC_DIR)/ui/main_window.cpp \
    $(SRC_DIR)/core/window_hide.cpp \
    $(SRC_DIR)/core/process_control.cpp \
    $(SRC_DIR)/core/driver_control.cpp \
    $(SRC_DIR)/core/mythware_control.cpp \
    $(SRC_DIR)/core/password_calc.cpp \
    $(SRC_DIR)/core/inject.cpp \
    $(SRC_DIR)/utils/log.cpp \
    $(SRC_DIR)/utils/window_utils.cpp \
    $(SRC_DIR)/utils/persist.cpp

# DLL 源文件（注入用）
DLL_SRC := $(DLL_DIR)/hide_hook.cpp

# 通用编译选项
WARN     := -Wall -Wextra
OPT      := -O2
CXXFLAGS := $(WARN) $(OPT) -std=c++17 -Iinclude -static-libstdc++ -static-libgcc
LDFLAGS  := -mwindows -static -lws2_32 -pthread -luser32 -lshell32 -lpsapi -ladvapi32 -ldwmapi -lgdi32 -lcomctl32 -lversion

# 架构特定标志已在上面设置
CXXFLAGS += $(ARCH_FLAG)
LDFLAGS  += $(ARCH_FLAG)

# DLL 编译选项（不含 -mwindows）
DLL_CXXFLAGS := $(WARN) $(OPT) -std=c++17 -DBUILD_DLL -shared -Iinclude $(ARCH_FLAG) -static-libstdc++ -static-libgcc -luser32

# 目标
MAIN_EXE := $(BIN_DIR)/MythwareHacker_$(ARCH_SUFFIX).exe
DLL_LIB  := $(BIN_DIR)/MythwareHideHook_$(ARCH_SUFFIX).dll
HELPER_EXE := $(BIN_DIR)/inject_helper_x86.exe

# 对象文件
MAIN_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(MAIN_SRCS))

# 默认目标
.PHONY: all clean dirs

all: dirs $(DLL_LIB) $(MAIN_EXE) $(HELPER_EXE)

dirs:
	@mkdir -p $(OBJ_DIR)/ui $(OBJ_DIR)/core $(OBJ_DIR)/utils $(BIN_DIR)

# 主程序
$(MAIN_EXE): $(MAIN_OBJS)
	$(CXX) $(MAIN_OBJS) -o $@ $(LDFLAGS)
	@echo "[OK] Built $(MAIN_EXE)"

# DLL
$(DLL_LIB): $(DLL_SRC) dirs
	$(CXX) $(DLL_SRC) -o $@ $(DLL_CXXFLAGS) -s
	@echo "[OK] Built $(DLL_LIB)"

# 32位注入辅助程序（始终用32位编译器）
$(HELPER_EXE): $(SRC_DIR)/tools/inject_helper.cpp dirs
	i686-w64-mingw32-g++ $(SRC_DIR)/tools/inject_helper.cpp -o $@ -O2 -s -static-libstdc++ -static-libgcc -m32
	@echo "[OK] Built $(HELPER_EXE)"

# 通用编译规则
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf bin/obj_32 bin/obj_64
	@rm -f $(BIN_DIR)/MythwareHacker_x86.exe $(BIN_DIR)/MythwareHacker_x64.exe
	@rm -f $(BIN_DIR)/MythwareHideHook_x86.dll $(BIN_DIR)/MythwareHideHook_x64.dll
	@rm -f $(BIN_DIR)/inject_helper_x86.exe
	@echo "[OK] Cleaned"
