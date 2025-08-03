@echo off
setlocal enabledelayedexpansion
::chcp 65001 >nul

:: ===========================================
:: 配置区域 - 分别指定64位和32位DLL文件
:: ===========================================
:: 64位注册表要注册的DLL文件（通常是x64版本）
set "DLL_LIST_X64="
set "DLL_LIST_X64=!DLL_LIST_X64! TimerResolution_x64.dll"
:: 添加更多64位DLL请按上述格式继续添加

:: 32位注册表要注册的DLL文件（通常是x86版本）
set "DLL_LIST_X86="
set "DLL_LIST_X86=!DLL_LIST_X86! TimerResolution_x86.dll"
:: 添加更多32位DLL请按上述格式继续添加
:: ===========================================

:: 获取脚本所在目录
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: 注册表路径
set "REG_PATH_64=HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows"
set "REG_PATH_32=HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Windows"

:MENU
cls
echo ========================================
echo           DLL注册表管理工具
echo ========================================
echo.
echo 当前目录: %SCRIPT_DIR%
echo 64位DLL配置: %DLL_LIST_X64%
echo 32位DLL配置: %DLL_LIST_X86%
echo.
echo 请选择操作:
echo 1. 安装
echo 2. 卸载
echo 3. 查看
echo.
set /p choice=请输入选项 (1-3): 

if "%choice%"=="1" goto REGISTER_ALL_DLLS
if "%choice%"=="2" goto UNREGISTER_ALL_DLLS
if "%choice%"=="3" goto VIEW_STATUS
echo 无效选项 请重新选择！
pause
goto MENU

:REGISTER_ALL_DLLS
cls
echo ========================================
echo         注册所有配置的DLL文件
echo ========================================
echo.

:: 检查64位DLL文件
echo 正在检查64位DLL文件...
set "dll_count_x64=0"
if not "%DLL_LIST_X64%"=="" (
    for %%d in (%DLL_LIST_X64%) do (
        if exist "%SCRIPT_DIR%\%%d" (
            set /a dll_count_x64+=1
            set "dll_x64_!dll_count_x64!=%SCRIPT_DIR%\%%d"
            echo [64位] %%d [存在] - 将注册
        ) else (
            echo [64位] %%d [不存在] - 跳过
        )
    )
)

:: 检查32位DLL文件
echo.
echo 正在检查32位DLL文件...
set "dll_count_x86=0"
if not "%DLL_LIST_X86%"=="" (
    for %%d in (%DLL_LIST_X86%) do (
        if exist "%SCRIPT_DIR%\%%d" (
            set /a dll_count_x86+=1
            set "dll_x86_!dll_count_x86!=%SCRIPT_DIR%\%%d"
            echo [32位] %%d [存在] - 将注册
        ) else (
            echo [32位] %%d [不存在] - 跳过
        )
    )
)

if %dll_count_x64%==0 (
    if %dll_count_x86%==0 (
        echo.
        echo 警告：未找到任何配置的DLL文件！
        echo 请检查配置区域中的DLL文件名是否正确 以及文件是否存在于当前目录 
        pause
        goto MENU
    )
)

echo.
echo 开始注册DLL文件...
echo.

:: 注册64位DLL
if %dll_count_x64% gtr 0 (
    echo === 注册64位DLL文件到64位注册表 ===
    for /l %%i in (1,1,%dll_count_x64%) do (
        set "current_dll=!dll_x64_%%i!"
        echo 正在注册: !current_dll!
        call :ADD_DLL_TO_REGISTRY "%REG_PATH_64%" "!current_dll!"
    )
    echo 64位DLL注册完成！
    echo.
)

:: 注册32位DLL
if %dll_count_x86% gtr 0 (
    echo === 注册32位DLL文件到32位注册表 ===
    for /l %%i in (1,1,%dll_count_x86%) do (
        set "current_dll=!dll_x86_%%i!"
        echo 正在注册: !current_dll!
        call :ADD_DLL_TO_REGISTRY "%REG_PATH_32%" "!current_dll!"
    )
    echo 32位DLL注册完成！
    echo.
)

echo ========================================
echo 所有配置的DLL文件注册完成！
echo ========================================
pause
goto MENU

:UNREGISTER_ALL_DLLS
cls
echo ========================================
echo         删除所有配置的DLL文件
echo ========================================
echo.

:: 统计配置的DLL文件
set "dll_count_x64=0"
if not "%DLL_LIST_X64%"=="" (
    for %%d in (%DLL_LIST_X64%) do (
        set "full_path=%SCRIPT_DIR%\%%d"
        set /a dll_count_x64+=1
        set "dll_x64_!dll_count_x64!=!full_path!"
        echo [64位] %%d - 将从64位注册表删除
    )
)

set "dll_count_x86=0"
if not "%DLL_LIST_X86%"=="" (
    for %%d in (%DLL_LIST_X86%) do (
        set "full_path=%SCRIPT_DIR%\%%d"
        set /a dll_count_x86+=1
        set "dll_x86_!dll_count_x86!=!full_path!"
        echo [32位] %%d - 将从32位注册表删除
    )
)

if %dll_count_x64%==0 (
    if %dll_count_x86%==0 (
        echo.
        echo 警告：配置中未指定任何DLL文件！
        echo 请检查配置区域中的DLL_LIST_X64和DLL_LIST_X86设置 
        pause
        goto MENU
    )
)

echo.
echo 开始删除DLL文件...
echo.

:: 删除64位DLL
if %dll_count_x64% gtr 0 (
    echo === 从64位注册表删除DLL文件 ===
    for /l %%i in (1,1,%dll_count_x64%) do (
        set "current_dll=!dll_x64_%%i!"
        echo 正在删除: !current_dll!
        call :REMOVE_DLL_FROM_REGISTRY "%REG_PATH_64%" "!current_dll!"
    )
    echo 64位DLL删除完成！
    echo.
)

:: 删除32位DLL
if %dll_count_x86% gtr 0 (
    echo === 从32位注册表删除DLL文件 ===
    for /l %%i in (1,1,%dll_count_x86%) do (
        set "current_dll=!dll_x86_%%i!"
        echo 正在删除: !current_dll!
        call :REMOVE_DLL_FROM_REGISTRY "%REG_PATH_32%" "!current_dll!"
    )
    echo 32位DLL删除完成！
    echo.
)

echo ========================================
echo 所有配置的DLL文件删除完成！
echo ========================================
pause
goto MENU

:VIEW_STATUS
cls
echo ========================================
echo           当前注册表状态
echo ========================================
echo.
echo 64位注册表状态:
echo ----------------
reg query "%REG_PATH_64%" /v AppInit_DLLs 2>nul
reg query "%REG_PATH_64%" /v LoadAppInit_DLLs 2>nul
reg query "%REG_PATH_64%" /v RequireSignedAppInit_DLLs 2>nul

echo.
echo 32位注册表状态:
echo ----------------
reg query "%REG_PATH_32%" /v AppInit_DLLs 2>nul
reg query "%REG_PATH_32%" /v LoadAppInit_DLLs 2>nul
reg query "%REG_PATH_32%" /v RequireSignedAppInit_DLLs 2>nul

echo.
pause
goto MENU

:ADD_DLL_TO_REGISTRY
set "reg_path=%~1"
set "dll_path=%~2"

:: 获取当前AppInit_DLLs的值
for /f "tokens=2*" %%a in ('reg query "%reg_path%" /v AppInit_DLLs 2^>nul ^| find "AppInit_DLLs"') do (
    set "current_value=%%b"
)

:: 如果没有找到现有值 设置为空
if not defined current_value set "current_value="

:: 检查DLL是否已经存在
echo !current_value! | find /i "%dll_path%" >nul
if not errorlevel 1 (
    echo DLL已存在于注册表中 跳过添加 
    goto SET_OTHER_VALUES
)

:: 添加DLL到现有值
if "!current_value!"=="" (
    set "new_value=%dll_path%"
) else (
    set "new_value=!current_value!,%dll_path%"
)

:: 写入AppInit_DLLs
reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "!new_value!" /f >nul

:SET_OTHER_VALUES
:: 设置LoadAppInit_DLLs为1
reg add "%reg_path%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul

:: 设置RequireSignedAppInit_DLLs为0
reg add "%reg_path%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul

goto :eof

:REMOVE_DLL_FROM_REGISTRY
set "reg_path=%~1"
set "dll_path=%~2"

:: 获取当前AppInit_DLLs的值
set "current_value="
set "value_found=0"
for /f "tokens=2*" %%a in ('reg query "%reg_path%" /v AppInit_DLLs 2^>nul ^| find "AppInit_DLLs"') do (
    set "current_value=%%b"
    set "value_found=1"
)

:: 如果没有找到现有值 创建空值并直接设置其他值
if "!value_found!"=="0" (
    echo 注册表中未找到AppInit_DLLs值 创建空值 
    reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "" /f >nul
    goto SET_OTHER_VALUES_REMOVE
)

:: 如果找到的值为空 也直接设置其他值
if "!current_value!"=="" (
    echo AppInit_DLLs值为空 
    goto SET_OTHER_VALUES_REMOVE
)

:: 检查DLL是否存在
echo !current_value! | find /i "%dll_path%" >nul
if errorlevel 1 (
    echo DLL不存在于注册表中 
    goto SET_OTHER_VALUES_REMOVE
)

:: 移除指定的DLL - 处理各种可能的位置
set "new_value=!current_value!"

:: 处理DLL在开头的情况 (dll,other -> other)
set "new_value=!new_value:%dll_path%,=!"

:: 处理DLL在中间的情况 (other,dll,another -> other,another)
set "new_value=!new_value:,%dll_path%,=,!"

:: 处理DLL在末尾的情况 (other,dll -> other)
set "new_value=!new_value:,%dll_path%=!"

:: 处理DLL是唯一值的情况 (dll -> "")
if "!new_value!"=="%dll_path%" set "new_value="

:: 清理可能残留的问题
:: 处理开头的逗号
if not "!new_value!"=="" (
    if "!new_value:~0,1!"=="," set "new_value=!new_value:~1!"
)

:: 处理结尾的逗号
if not "!new_value!"=="" (
    if "!new_value:~-1!"=="," set "new_value=!new_value:~0,-1!"
)

:: 处理连续的逗号
:CLEAN_COMMAS
if not "!new_value!"=="" (
    set "temp_value=!new_value:,,=,!"
    if not "!temp_value!"=="!new_value!" (
        set "new_value=!temp_value!"
        goto CLEAN_COMMAS
    )
)

:: 如果结果只是逗号或空白 则清空
if "!new_value!"=="," set "new_value="

:: 写入更新后的值
reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "!new_value!" /f >nul

:SET_OTHER_VALUES_REMOVE
:: 设置LoadAppInit_DLLs为1
reg add "%reg_path%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul

:: 设置RequireSignedAppInit_DLLs为0
reg add "%reg_path%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul

goto :eof

:EXIT
exit /b 0