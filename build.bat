@echo off
setlocal enabledelayedexpansion
title 构建 SOCD Cleaner...

echo ============================================
echo   SOCD Cleaner — 编译脚本
echo ============================================
echo.

:: ── 查找 Visual Studio ────────────────────────────────────────────────
set "VSCMD="

:: VS 2022+
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo [INFO] 找到 Visual Studio 2022 Community
    set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    echo [INFO] 找到 Visual Studio 2022 Professional
    set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    echo [INFO] 找到 Visual Studio 2022 Enterprise
    set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

:: VS 18 (Dev Box / Preview)
if not defined VSCMD if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo [INFO] 找到 Visual Studio 18 Community
    set "VSCMD=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)

:: VS 2019
if not defined VSCMD if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo [INFO] 找到 Visual Studio 2019 Community
    set "VSCMD=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)

:: vswhere 自动探测
if not defined VSCMD if exist "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo [INFO] 通过 vswhere 查找 Visual Studio...
    for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
        set "VSPATH=%%i"
    )
    if exist "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [INFO] 找到 Visual Studio: !VSPATH!
        set "VSCMD=!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
    )
)

:: cl.exe 已在 PATH 中
if not defined VSCMD (
    where cl.exe >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] cl.exe 已在 PATH 中
        goto :compile
    )
)

if not defined VSCMD (
    echo [ERROR] 未找到 Visual Studio 或 MSVC 构建工具。
    echo.
    echo 请安装以下之一：
    echo   1. Visual Studio 2022 Community（免费）
    echo      https://visualstudio.microsoft.com/vs/community/
    echo      安装时勾选"使用 C++ 的桌面开发"工作负载
    echo.
    echo   2. Visual Studio Build Tools（轻量版，无 IDE）
    echo      https://visualstudio.microsoft.com/downloads/
    echo      安装时勾选"MSVC"和"Windows SDK"组件
    echo.
    exit /b 1
)

call "%VSCMD%" >nul 2>&1

:: ── 编译 ───────────────────────────────────────────────────────────────
:compile
echo.
echo [INFO] 正在编译 SOCD Cleaner...

:: 清理旧产物
if exist socd.exe   del socd.exe
if exist *.obj      del *.obj 2>nul

cl.exe /nologo /EHsc /O2 /W4 /std:c++17 /utf-8 /DUNICODE /D_UNICODE ^
    /Fe:socd.exe main.cpp socd_engine.cpp ^
    /link user32.lib gdi32.lib shell32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] 编译失败！
    exit /b 1
)

echo.
echo ============================================
echo   编译成功！socd.exe 已生成
echo ============================================
echo.
echo 运行 socd.exe 启动，托盘图标右键可切换模式。
echo 按下 A+D 在记事本里试试 SOCD 效果。
echo.
exit /b 0
