@echo off
REM MythwareHacker - Windows 构建脚本
REM 依赖：MinGW-w64 (g++)，需在 PATH 中
REM 用法：
REM   build.bat           构建当前架构
REM   build.bat 32        强制 32 位
REM   build.bat 64        强制 64 位
REM   build.bat all       同时构建 32 和 64 位
REM   build.bat clean     清理

setlocal enabledelayedexpansion

set ARCH=%1
if "%ARCH%"=="" set ARCH=current

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH. Please install MinGW-w64.
    exit /b 1
)

if /i "%ARCH%"=="clean" (
    if exist bin\obj_32 rmdir /s /q bin\obj_32
    if exist bin\obj_64 rmdir /s /q bin\obj_64
    del /q bin\MythwareHacker_x86.exe 2>nul
    del /q bin\MythwareHacker_x64.exe 2>nul
    del /q bin\MythwareHideHook_x86.dll 2>nul
    del /q bin\MythwareHideHook_x64.dll 2>nul
    echo [OK] Cleaned.
    exit /b 0
)

if /i "%ARCH%"=="all" (
    call :build_arch 32
    if errorlevel 1 exit /b 1
    call :build_arch 64
    exit /b %errorlevel%
)

if /i "%ARCH%"=="32" (
    call :build_arch 32
    exit /b %errorlevel%
)

if /i "%ARCH%"=="64" (
    call :build_arch 64
    exit /b %errorlevel%
)

REM 默认：根据 g++ 自身位数构建
echo [INFO] Building native architecture...
call :build_arch native
exit /b %errorlevel%

:build_arch
set BUILD_ARCH=%1
set ARCH_FLAG=
set ARCH_SUFFIX=

if "%BUILD_ARCH%"=="32" (
    set ARCH_FLAG=-m32
    set ARCH_SUFFIX=x86
) else if "%BUILD_ARCH%"=="64" (
    set ARCH_FLAG=-m64
    set ARCH_SUFFIX=x64
) else (
    set ARCH_SUFFIX=native
)

if not exist bin mkdir bin
if not exist bin\obj_%ARCH_SUFFIX%\ui mkdir bin\obj_%ARCH_SUFFIX%\ui
if not exist bin\obj_%ARCH_SUFFIX%\core mkdir bin\obj_%ARCH_SUFFIX%\core
if not exist bin\obj_%ARCH_SUFFIX%\utils mkdir bin\obj_%ARCH_SUFFIX%\utils

echo.
echo === Building DLL (%ARCH_SUFFIX%) ===
g++ -DBUILD_DLL -shared -o bin\MythwareHideHook_%ARCH_SUFFIX%.dll ^
    src\dll\hide_hook.cpp -O2 -s -Iinclude %ARCH_FLAG% ^
    -static-libstdc++ -static-libgcc -luser32
if errorlevel 1 (
    echo [ERROR] DLL build failed.
    exit /b 1
)
echo [OK] bin\MythwareHideHook_%ARCH_SUFFIX%.dll

echo.
echo === Building main EXE (%ARCH_SUFFIX%) ===

set SRCS=^
    src\main.cpp ^
    src\ui\tray.cpp ^
    src\ui\float_window.cpp ^
    src\ui\preview.cpp ^
    src\ui\hotkey.cpp ^
    src\ui\menu.cpp ^
    src\ui\main_window.cpp ^
    src\core\window_hide.cpp ^
    src\core\process_control.cpp ^
    src\core\driver_control.cpp ^
    src\core\mythware_control.cpp ^
    src\core\password_calc.cpp ^
    src\core\inject.cpp ^
    src\utils\log.cpp ^
    src\utils\window_utils.cpp ^
    src\utils\persist.cpp

g++ %SRCS% -o bin\MythwareHacker_%ARCH_SUFFIX%.exe ^
    -O2 -std=c++17 -Iinclude %ARCH_FLAG% ^
    -mwindows -static -static-libstdc++ -static-libgcc ^
    -lws2_32 -pthread -luser32 -lshell32 -lpsapi -ladvapi32 -ldwmapi -lgdi32 -lcomctl32 -lversion
if errorlevel 1 (
    echo [ERROR] EXE build failed.
    exit /b 1
)
echo [OK] bin\MythwareHacker_%ARCH_SUFFIX%.exe
exit /b 0
