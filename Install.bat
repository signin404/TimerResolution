@echo off
setlocal enabledelayedexpansion

:: --- Configuration ---
set "DLL_NAME_X64=TimerResolution_x64.dll"
set "DLL_NAME_X86=TimerResolution_x86.dll"

set "REG_KEY_64=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows"
set "REG_KEY_32=HKLM\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\Windows"
set "REG_VALUE_NAME=AppInit_DLLs"

:menu
cls
echo.
echo  -----------------------------------------------------
echo   1. 安装
echo   2. 卸载
echo   3. 退出
echo  -----------------------------------------------------
echo.

choice /c 123 /n /m "请选择选项: "

if errorlevel 3 goto :end
if errorlevel 2 goto :uninstall
if errorlevel 1 goto :install

:install
cls
echo.
echo  [ INSTALLING ]
echo  ------------
echo.

rem --- Configure required registry settings for AppInit_DLLs to work ---
::echo  -> Enabling LoadAppInit_DLLs mechanism...
reg add "%REG_KEY_64%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul
reg add "%REG_KEY_32%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul
::echo  -> Disabling signature requirement (for non-signed DLLs)...
reg add "%REG_KEY_64%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul
reg add "%REG_KEY_32%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul
echo.

rem --- Process 64-bit DLL ---
set "DLL_PATH=%~dp0%DLL_NAME_X64%"
if not exist "%DLL_PATH%" (
    echo  ERROR: 64-bit DLL not found at %DLL_PATH%
) else (
    echo  Processing 64-bit registry key...
    call :add_value "%REG_KEY_64%" "%DLL_PATH%"
)
echo.

rem --- Process 32-bit DLL ---
set "DLL_PATH=%~dp0%DLL_NAME_X86%"
if not exist "%DLL_PATH%" (
    echo  ERROR: 32-bit DLL not found at %DLL_PATH%
) else (
    echo  Processing 32-bit registry key...
    call :add_value "%REG_KEY_32%" "%DLL_PATH%"
)

echo.
echo  安装完成.
goto :end

:uninstall
cls
echo.
echo  [ UNINSTALLING ]
echo  --------------
echo.

rem --- Process 64-bit DLL ---
set "DLL_PATH=%~dp0%DLL_NAME_X64%"
echo  Processing 64-bit registry key...
call :remove_value "%REG_KEY_64%" "%DLL_PATH%"
echo.

rem --- Process 32-bit DLL ---
set "DLL_PATH=%~dp0%DLL_NAME_X86%"
echo  Processing 32-bit registry key...
call :remove_value "%REG_KEY_32%" "%DLL_PATH%"

echo.
echo  卸载完成.
goto :end


:: =================================================================
:: SUBROUTINES
:: =================================================================

:add_value
    set "REG_KEY=%~1"
    set "DLL_TO_ADD=%~2"
    set "CURRENT_VALUE="

    rem --- Query the current registry value without causing an error if it doesn't exist ---
    for /f "tokens=2,*" %%a in ('reg query "%REG_KEY%" /v %REG_VALUE_NAME% 2^>nul') do (
        if /i "%%a"=="REG_SZ" (
            set "CURRENT_VALUE=%%b"
        )
    )

    if not defined CURRENT_VALUE (
        rem --- Value is empty, just add our DLL ---
        echo     Value is empty. Creating new value.
        set "NEW_VALUE=%DLL_TO_ADD%"
    ) else (
        rem --- Value exists, check if our DLL is already there ---
        echo "!CURRENT_VALUE!" | find /i "%DLL_TO_ADD%" >nul
        if !errorlevel! equ 0 (
            echo     DLL path already exists. No changes made.
            goto :eof
        )
        rem --- Append our DLL to the existing value ---
        echo     Value exists. Appending DLL path.
        set "NEW_VALUE=%CURRENT_VALUE%,%DLL_TO_ADD%"
    )

    reg add "%REG_KEY%" /v %REG_VALUE_NAME% /t REG_SZ /d "!NEW_VALUE!" /f >nul
    echo     Successfully updated registry.
goto :eof


:remove_value
    set "REG_KEY=%~1"
    set "DLL_TO_REMOVE=%~2"
    set "CURRENT_VALUE="

    rem --- Query the current registry value ---
    for /f "tokens=2,*" %%a in ('reg query "%REG_KEY%" /v %REG_VALUE_NAME% 2^>nul') do (
        if /i "%%a"=="REG_SZ" (
            set "CURRENT_VALUE=%%b"
        )
    )

    if not defined CURRENT_VALUE (
        echo     Value is empty. Nothing to remove.
        goto :eof
    )

    rem --- Check if our DLL path is in the value ---
    echo "!CURRENT_VALUE!" | find /i "%DLL_TO_REMOVE%" >nul
    if !errorlevel! neq 0 (
        echo     DLL path not found in value. Nothing to remove.
        goto :eof
    )

    rem --- Perform the removal and cleanup commas ---
    set "CLEANED_VALUE=!CURRENT_VALUE:%DLL_TO_REMOVE%=!"
    set "CLEANED_VALUE=!CLEANED_VALUE:,,=,!"
    if "!CLEANED_VALUE:~0,1!"=="," set "CLEANED_VALUE=!CLEANED_VALUE:~1!"
    if "!CLEANED_VALUE:~-1!"=="," set "CLEANED_VALUE=!CLEANED_VALUE:~0,-1!"

    if defined CLEANED_VALUE (
        rem --- Other DLLs remain, update the value ---
        echo     Removing DLL path and updating value.
        reg add "%REG_KEY%" /v %REG_VALUE_NAME% /t REG_SZ /d "!CLEANED_VALUE!" /f >nul
    ) else (
        rem --- Our DLL was the only one, delete the value completely ---
        echo     DLL was the only entry. Deleting value.
        reg delete "%REG_KEY%" /v %REG_VALUE_NAME% /f >nul
    )
    echo     Successfully updated registry.
goto :eof

:end
echo.
pause
exit
