// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>

#pragma comment(lib, "User32.lib")

// 手动定义 NT_SUCCESS 宏
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

// 定义函数指针类型
typedef NTSTATUS(NTAPI* pfnGenericTimerApi)(ULONG, BOOLEAN, PULONG);

// 全局变量
pfnGenericTimerApi g_pfnSetTimerResolution = nullptr;
static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hThread = NULL;
static ULONG g_targetResolution = 5000;
static bool g_isSpecialProcess = false;
// 新增：用于存储检测间隔时间，默认10秒
static DWORD g_checkInterval = 10000; 

// 监控线程的主函数
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    const DWORD currentProcessId = GetCurrentProcessId();
    ULONG currentResolution;
    while (g_runThread) {
        if (!g_pfnSetTimerResolution) { Sleep(1000); continue; }
        HWND hForegroundWnd = GetForegroundWindow();
        bool isForeground = false;
        if (hForegroundWnd) {
            DWORD foregroundProcessId = 0;
            GetWindowThreadProcessId(hForegroundWnd, &foregroundProcessId);
            if (foregroundProcessId == currentProcessId) { isForeground = true; }
        }
        if (isForeground) {
            if (!g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution))) { g_isTimerHigh = true; }
            }
        } else {
            if (g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution))) { g_isTimerHigh = false; }
            }
        }
        // 使用从INI读取的配置作为检测间隔
        Sleep(g_checkInterval);
    }
    if (g_isTimerHigh && g_pfnSetTimerResolution) {
        g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
        g_isTimerHigh = false;
    }
    return 0;
}

// 导出函数
extern "C" __declspec(dllexport) void PlaceholderExport() {}

// DLL入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // 1. 获取进程名和INI路径
        wchar_t processPath[MAX_PATH];
        GetModuleFileNameW(NULL, processPath, MAX_PATH);
        const wchar_t* processName = wcsrchr(processPath, L'\\');
        processName = (processName) ? processName + 1 : processPath;
        
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
        if (lastSlash) { *(lastSlash + 1) = L'\0'; }
        std::wstring iniPath = dllPath;
        iniPath += L"Config.ini";

        // 2. 黑名单检查
        wchar_t checkBuffer[2];
        GetPrivateProfileStringW(L"BlackList", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0) { return TRUE; }

        // 3. 读取INI配置
        g_targetResolution = GetPrivateProfileIntW(L"Settings", L"Resolution", 5000, iniPath.c_str());
        if (g_targetResolution == 0) { g_targetResolution = 5000; }
        
        // =======================================================================
        //  ↓↓↓ 核心改动：读取前后台检测的时间间隔配置 ↓↓↓
        // =======================================================================
        g_checkInterval = GetPrivateProfileIntW(L"Settings", L"CheckInterval", 10000, iniPath.c_str());
        if (g_checkInterval < 1000) { g_checkInterval = 1000; } // 增加一个最小间隔限制，防止过于频繁的检测
        // =======================================================================

        // 4. 获取函数地址
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        if (!g_pfnSetTimerResolution) { return TRUE; }

        // 5. 检查特殊进程
        GetPrivateProfileStringW(L"PersistentProcesses", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        
        if (wcscmp(checkBuffer, L"0") != 0)
        {
            // 如果存在，则为特殊进程
            g_isSpecialProcess = true;
            ULONG currentResolution;
            g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution);
        }
        else
        {
            // 如果不存在，则为普通进程
            g_isSpecialProcess = false;
            DisableThreadLibraryCalls(hModule);
            g_runThread = true;
            g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        }
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // 清理逻辑完全不变
        if (g_isSpecialProcess)
        {
            if (g_pfnSetTimerResolution) {
                ULONG currentResolution;
                g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
            }
        }
        else
        {
            if (g_hThread)
            {
                g_runThread = false;
                WaitForSingleObject(g_hThread, 5000);
                CloseHandle(g_hThread);
                g_hThread = NULL;
            }
        }
    }
    return TRUE;
}