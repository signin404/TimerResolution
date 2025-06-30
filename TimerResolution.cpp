// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>
#include <avrt.h>      // For MMCSS functions

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Avrt.lib")   // Link the Avrt library for MMCSS

// 手动定义 NT_SUCCESS 宏
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

// 定义函数指针类型
typedef NTSTATUS(NTAPI* pfnGenericTimerApi)(ULONG, BOOLEAN, PULONG);

// 全局变量
pfnGenericTimerApi g_pfnSetTimerResolution = nullptr;
static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hTimerThread = NULL;
static ULONG g_targetResolution = 5000;
static bool g_isSpecialProcess = false;
static HANDLE g_hMmcssThread = NULL;
static HANDLE g_hShutdownEvent = NULL;

// =======================================================================
//  ↓↓↓ 修复：恢复 TimerMonitorThread 的完整函数体 ↓↓↓
// =======================================================================
// 计时器精度监控线程
DWORD WINAPI TimerMonitorThread(LPVOID lpParam)
{
    const DWORD currentProcessId = GetCurrentProcessId();
    ULONG currentResolution;

    while (g_runThread)
    {
        if (!g_pfnSetTimerResolution) {
            Sleep(1000);
            continue;
        }

        HWND hForegroundWnd = GetForegroundWindow();
        bool isForeground = false;
        if (hForegroundWnd)
        {
            DWORD foregroundProcessId = 0;
            GetWindowThreadProcessId(hForegroundWnd, &foregroundProcessId);
            if (foregroundProcessId == currentProcessId)
            {
                isForeground = true;
            }
        }

        if (isForeground) {
            if (!g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution))) {
                    g_isTimerHigh = true;
                }
            }
        } else {
            if (g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution))) {
                    g_isTimerHigh = false;
                }
            }
        }
        Sleep(250);
    }

    if (g_isTimerHigh && g_pfnSetTimerResolution)
    {
        g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
        g_isTimerHigh = false;
    }

    return 0; // 确保函数返回一个值
}
// =======================================================================

// MMCSS专用线程的主函数
DWORD WINAPI MmcssRegistrationThread(LPVOID lpParam)
{
    wchar_t* taskName = (wchar_t*)lpParam;
    DWORD taskIndex = 0;
    HANDLE hMmcssTask = NULL;

    hMmcssTask = AvSetMmThreadCharacteristicsW(taskName, &taskIndex);
    delete[] taskName;

    if (hMmcssTask)
    {
        WaitForSingleObject(g_hShutdownEvent, INFINITE);
        AvRevertMmThreadCharacteristics(hMmcssTask);
    }

    return 0; // 确保函数返回一个值
}

// 导出函数
extern "C" __declspec(dllexport) void PlaceholderExport() {}

// DLL入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        wchar_t processPath[MAX_PATH];
        GetModuleFileNameW(NULL, processPath, MAX_PATH);
        const wchar_t* processName = wcsrchr(processPath, L'\\');
        processName = (processName) ? processName + 1 : processPath;
        
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
        if (lastSlash) { *(lastSlash + 1) = L'\0'; }
        std::wstring iniPath = dllPath;
        iniPath += L"blacklist.ini";

        wchar_t checkBuffer[2];
        GetPrivateProfileStringW(L"Blacklist", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0) { return TRUE; }

        g_targetResolution = GetPrivateProfileIntW(L"Settings", L"Resolution", 5000, iniPath.c_str());
        if (g_targetResolution == 0) { g_targetResolution = 5000; }

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        if (!g_pfnSetTimerResolution) { return TRUE; }

        GetPrivateProfileStringW(L"PersistentProcesses", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0)
        {
            g_isSpecialProcess = true;
            ULONG currentResolution;
            g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution);
        }
        else
        {
            g_isSpecialProcess = false;
            DisableThreadLibraryCalls(hModule);
            g_runThread = true;
            g_hTimerThread = CreateThread(NULL, 0, TimerMonitorThread, NULL, 0, NULL);

            if (GetPrivateProfileIntW(L"MMCSS", L"Enabled", 0, iniPath.c_str()) == 1)
            {
                g_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (g_hShutdownEvent) {
                    wchar_t* taskName = new wchar_t[64];
                    GetPrivateProfileStringW(L"MMCSS", L"TaskName", L"Pro Audio", taskName, 64, iniPath.c_str());
                    g_hMmcssThread = CreateThread(NULL, 0, MmcssRegistrationThread, taskName, 0, NULL);
                }
            }
        }
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        if (g_isSpecialProcess)
        {
            if (g_pfnSetTimerResolution) {
                ULONG currentResolution;
                g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
            }
        }
        else
        {
            if (g_hTimerThread)
            {
                g_runThread = false;
                WaitForSingleObject(g_hTimerThread, 5000);
                CloseHandle(g_hTimerThread);
            }
        }

        if (g_hMmcssThread) {
            if (g_hShutdownEvent) SetEvent(g_hShutdownEvent);
            WaitForSingleObject(g_hMmcssThread, 5000);
            CloseHandle(g_hMmcssThread);
            if (g_hShutdownEvent) CloseHandle(g_hShutdownEvent);
        }
    }
    return TRUE;
}