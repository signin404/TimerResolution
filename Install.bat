@echo off
setlocal enabledelayedexpansion
::chcp 65001 >nul

:: ===========================================
:: �������� - �ֱ�ָ��64λ��32λDLL�ļ�
:: ===========================================
:: 64λע���Ҫע���DLL�ļ���ͨ����x64�汾��
set "DLL_LIST_X64="
set "DLL_LIST_X64=!DLL_LIST_X64! TimerResolution_x64.dll"
:: ��Ӹ���64λDLL�밴������ʽ�������

:: 32λע���Ҫע���DLL�ļ���ͨ����x86�汾��
set "DLL_LIST_X86="
set "DLL_LIST_X86=!DLL_LIST_X86! TimerResolution_x86.dll"
:: ��Ӹ���32λDLL�밴������ʽ�������
:: ===========================================

:: ��ȡ�ű�����Ŀ¼
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: ע���·��
set "REG_PATH_64=HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows"
set "REG_PATH_32=HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Windows"

:MENU
cls
echo ========================================
echo           DLLע��������
echo ========================================
echo.
echo ��ǰĿ¼: %SCRIPT_DIR%
echo 64λDLL����: %DLL_LIST_X64%
echo 32λDLL����: %DLL_LIST_X86%
echo.
echo ��ѡ�����:
echo 1. ��װ
echo 2. ж��
echo 3. �鿴
echo.
set /p choice=������ѡ�� (1-3): 

if "%choice%"=="1" goto REGISTER_ALL_DLLS
if "%choice%"=="2" goto UNREGISTER_ALL_DLLS
if "%choice%"=="3" goto VIEW_STATUS
echo ��Чѡ�� ������ѡ��
pause
goto MENU

:REGISTER_ALL_DLLS
cls
echo ========================================
echo         ע���������õ�DLL�ļ�
echo ========================================
echo.

:: ���64λDLL�ļ�
echo ���ڼ��64λDLL�ļ�...
set "dll_count_x64=0"
if not "%DLL_LIST_X64%"=="" (
    for %%d in (%DLL_LIST_X64%) do (
        if exist "%SCRIPT_DIR%\%%d" (
            set /a dll_count_x64+=1
            set "dll_x64_!dll_count_x64!=%SCRIPT_DIR%\%%d"
            echo [64λ] %%d [����] - ��ע��
        ) else (
            echo [64λ] %%d [������] - ����
        )
    )
)

:: ���32λDLL�ļ�
echo.
echo ���ڼ��32λDLL�ļ�...
set "dll_count_x86=0"
if not "%DLL_LIST_X86%"=="" (
    for %%d in (%DLL_LIST_X86%) do (
        if exist "%SCRIPT_DIR%\%%d" (
            set /a dll_count_x86+=1
            set "dll_x86_!dll_count_x86!=%SCRIPT_DIR%\%%d"
            echo [32λ] %%d [����] - ��ע��
        ) else (
            echo [32λ] %%d [������] - ����
        )
    )
)

if %dll_count_x64%==0 (
    if %dll_count_x86%==0 (
        echo.
        echo ���棺δ�ҵ��κ����õ�DLL�ļ���
        echo �������������е�DLL�ļ����Ƿ���ȷ �Լ��ļ��Ƿ�����ڵ�ǰĿ¼ 
        pause
        goto MENU
    )
)

echo.
echo ��ʼע��DLL�ļ�...
echo.

:: ע��64λDLL
if %dll_count_x64% gtr 0 (
    echo === ע��64λDLL�ļ���64λע��� ===
    for /l %%i in (1,1,%dll_count_x64%) do (
        set "current_dll=!dll_x64_%%i!"
        echo ����ע��: !current_dll!
        call :ADD_DLL_TO_REGISTRY "%REG_PATH_64%" "!current_dll!"
    )
    echo 64λDLLע����ɣ�
    echo.
)

:: ע��32λDLL
if %dll_count_x86% gtr 0 (
    echo === ע��32λDLL�ļ���32λע��� ===
    for /l %%i in (1,1,%dll_count_x86%) do (
        set "current_dll=!dll_x86_%%i!"
        echo ����ע��: !current_dll!
        call :ADD_DLL_TO_REGISTRY "%REG_PATH_32%" "!current_dll!"
    )
    echo 32λDLLע����ɣ�
    echo.
)

echo ========================================
echo �������õ�DLL�ļ�ע����ɣ�
echo ========================================
pause
goto MENU

:UNREGISTER_ALL_DLLS
cls
echo ========================================
echo         ɾ���������õ�DLL�ļ�
echo ========================================
echo.

:: ͳ�����õ�DLL�ļ�
set "dll_count_x64=0"
if not "%DLL_LIST_X64%"=="" (
    for %%d in (%DLL_LIST_X64%) do (
        set "full_path=%SCRIPT_DIR%\%%d"
        set /a dll_count_x64+=1
        set "dll_x64_!dll_count_x64!=!full_path!"
        echo [64λ] %%d - ����64λע���ɾ��
    )
)

set "dll_count_x86=0"
if not "%DLL_LIST_X86%"=="" (
    for %%d in (%DLL_LIST_X86%) do (
        set "full_path=%SCRIPT_DIR%\%%d"
        set /a dll_count_x86+=1
        set "dll_x86_!dll_count_x86!=!full_path!"
        echo [32λ] %%d - ����32λע���ɾ��
    )
)

if %dll_count_x64%==0 (
    if %dll_count_x86%==0 (
        echo.
        echo ���棺������δָ���κ�DLL�ļ���
        echo �������������е�DLL_LIST_X64��DLL_LIST_X86���� 
        pause
        goto MENU
    )
)

echo.
echo ��ʼɾ��DLL�ļ�...
echo.

:: ɾ��64λDLL
if %dll_count_x64% gtr 0 (
    echo === ��64λע���ɾ��DLL�ļ� ===
    for /l %%i in (1,1,%dll_count_x64%) do (
        set "current_dll=!dll_x64_%%i!"
        echo ����ɾ��: !current_dll!
        call :REMOVE_DLL_FROM_REGISTRY "%REG_PATH_64%" "!current_dll!"
    )
    echo 64λDLLɾ����ɣ�
    echo.
)

:: ɾ��32λDLL
if %dll_count_x86% gtr 0 (
    echo === ��32λע���ɾ��DLL�ļ� ===
    for /l %%i in (1,1,%dll_count_x86%) do (
        set "current_dll=!dll_x86_%%i!"
        echo ����ɾ��: !current_dll!
        call :REMOVE_DLL_FROM_REGISTRY "%REG_PATH_32%" "!current_dll!"
    )
    echo 32λDLLɾ����ɣ�
    echo.
)

echo ========================================
echo �������õ�DLL�ļ�ɾ����ɣ�
echo ========================================
pause
goto MENU

:VIEW_STATUS
cls
echo ========================================
echo           ��ǰע���״̬
echo ========================================
echo.
echo 64λע���״̬:
echo ----------------
reg query "%REG_PATH_64%" /v AppInit_DLLs 2>nul
reg query "%REG_PATH_64%" /v LoadAppInit_DLLs 2>nul
reg query "%REG_PATH_64%" /v RequireSignedAppInit_DLLs 2>nul

echo.
echo 32λע���״̬:
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

:: ��ȡ��ǰAppInit_DLLs��ֵ
for /f "tokens=2*" %%a in ('reg query "%reg_path%" /v AppInit_DLLs 2^>nul ^| find "AppInit_DLLs"') do (
    set "current_value=%%b"
)

:: ���û���ҵ�����ֵ ����Ϊ��
if not defined current_value set "current_value="

:: ���DLL�Ƿ��Ѿ�����
echo !current_value! | find /i "%dll_path%" >nul
if not errorlevel 1 (
    echo DLL�Ѵ�����ע����� ������� 
    goto SET_OTHER_VALUES
)

:: ���DLL������ֵ
if "!current_value!"=="" (
    set "new_value=%dll_path%"
) else (
    set "new_value=!current_value!,%dll_path%"
)

:: д��AppInit_DLLs
reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "!new_value!" /f >nul

:SET_OTHER_VALUES
:: ����LoadAppInit_DLLsΪ1
reg add "%reg_path%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul

:: ����RequireSignedAppInit_DLLsΪ0
reg add "%reg_path%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul

goto :eof

:REMOVE_DLL_FROM_REGISTRY
set "reg_path=%~1"
set "dll_path=%~2"

:: ��ȡ��ǰAppInit_DLLs��ֵ
set "current_value="
set "value_found=0"
for /f "tokens=2*" %%a in ('reg query "%reg_path%" /v AppInit_DLLs 2^>nul ^| find "AppInit_DLLs"') do (
    set "current_value=%%b"
    set "value_found=1"
)

:: ���û���ҵ�����ֵ ������ֵ��ֱ����������ֵ
if "!value_found!"=="0" (
    echo ע�����δ�ҵ�AppInit_DLLsֵ ������ֵ 
    reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "" /f >nul
    goto SET_OTHER_VALUES_REMOVE
)

:: ����ҵ���ֵΪ�� Ҳֱ����������ֵ
if "!current_value!"=="" (
    echo AppInit_DLLsֵΪ�� 
    goto SET_OTHER_VALUES_REMOVE
)

:: ���DLL�Ƿ����
echo !current_value! | find /i "%dll_path%" >nul
if errorlevel 1 (
    echo DLL��������ע����� 
    goto SET_OTHER_VALUES_REMOVE
)

:: �Ƴ�ָ����DLL - ������ֿ��ܵ�λ��
set "new_value=!current_value!"

:: ����DLL�ڿ�ͷ����� (dll,other -> other)
set "new_value=!new_value:%dll_path%,=!"

:: ����DLL���м����� (other,dll,another -> other,another)
set "new_value=!new_value:,%dll_path%,=,!"

:: ����DLL��ĩβ����� (other,dll -> other)
set "new_value=!new_value:,%dll_path%=!"

:: ����DLL��Ψһֵ����� (dll -> "")
if "!new_value!"=="%dll_path%" set "new_value="

:: ������ܲ���������
:: ����ͷ�Ķ���
if not "!new_value!"=="" (
    if "!new_value:~0,1!"=="," set "new_value=!new_value:~1!"
)

:: �����β�Ķ���
if not "!new_value!"=="" (
    if "!new_value:~-1!"=="," set "new_value=!new_value:~0,-1!"
)

:: ���������Ķ���
:CLEAN_COMMAS
if not "!new_value!"=="" (
    set "temp_value=!new_value:,,=,!"
    if not "!temp_value!"=="!new_value!" (
        set "new_value=!temp_value!"
        goto CLEAN_COMMAS
    )
)

:: ������ֻ�Ƕ��Ż�հ� �����
if "!new_value!"=="," set "new_value="

:: д����º��ֵ
reg add "%reg_path%" /v AppInit_DLLs /t REG_SZ /d "!new_value!" /f >nul

:SET_OTHER_VALUES_REMOVE
:: ����LoadAppInit_DLLsΪ1
reg add "%reg_path%" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >nul

:: ����RequireSignedAppInit_DLLsΪ0
reg add "%reg_path%" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >nul

goto :eof

:EXIT
exit /b 0