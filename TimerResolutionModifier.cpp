// TimerResolutionModifier.cpp
#include <Windows.h>
#include <timeapi.h>

// 链接所需的库
#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "User32.lib") // <-- 新增：链接 User32.lib 库以使用窗口相关的API

// 全局变量，用于线程控制和状态管理
static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hThread = NULL;

// 监控线程的主函数
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    const DWORD currentProcessId = GetCurrentProcessId();

    while (g_runThread)
    {
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

        if (isForeground)
        {
            if (!g_isTimerHigh)
            {
                if (timeBeginPeriod(1) == TIMERR_NOERROR)
                {
                    g_isTimerHigh = true;
                }
            }
        }
        else
        {
            if (g_isTimerHigh)
            {
                if (timeEndPeriod(1) == TIMERR_NOERROR)
                {
                    g_isTimerHigh = false;
                }
            }
        }

        Sleep(250);
    }

    if (g_isTimerHigh)
    {
        timeEndPeriod(1);
        g_isTimerHigh = false;
    }

    return 0;
}

// 导出函数，以满足某些注入器的要求
extern "C" __declspec(dllexport) void PlaceholderExport()
{
    // Do nothing.
}

// DLL入口点
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_runThread = true;
        g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        break;

    case DLL_PROCESS_DETACH:
        if (g_hThread)
        {
            g_runThread = false;
            WaitForSingleObject(g_hThread, 5000);
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
