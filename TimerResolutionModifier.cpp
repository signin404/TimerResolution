// TimerResolutionModifier.cpp
#include <Windows.h>
#include <timeapi.h>

#pragma comment(lib, "Winmm.lib")

// 全局变量，用于线程控制和状态管理
// volatile 关键字确保多线程访问时的可见性，防止编译器过度优化
static volatile bool g_runThread = false;        // 控制监控线程运行的标志
static volatile bool g_isTimerHigh = false;      // 记录当前计时器是否已处于高精度状态
static HANDLE g_hThread = NULL;                  // 监控线程的句柄

// 监控线程的主函数
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    // 获取当前进程（即被注入的进程）的ID
    const DWORD currentProcessId = GetCurrentProcessId();

    while (g_runThread)
    {
        // 获取当前前台窗口的句柄
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

        // 如果当前进程是前台进程...
        if (isForeground)
        {
            // ...并且计时器还不是高精度状态，则设置它
            if (!g_isTimerHigh)
            {
                if (timeBeginPeriod(1) == TIMERR_NOERROR)
                {
                    g_isTimerHigh = true;
                }
            }
        }
        // 如果当前进程是后台进程...
        else
        {
            // ...并且计时器当前是高精度状态，则恢复它
            if (g_isTimerHigh)
            {
                if (timeEndPeriod(1) == TIMERR_NOERROR)
                {
                    g_isTimerHigh = false;
                }
            }
        }

        // 等待一小段时间再进行下一次检查，以避免CPU占用过高
        // 250毫秒的检查频率对于前后台切换检测来说绰绰有余
        Sleep(250);
    }

    // 线程退出前，最后检查一次，确保恢复计时器精度
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
        // 优化：禁止对后续的线程创建/销毁事件调用DllMain，减少开销
        DisableThreadLibraryCalls(hModule);
        
        // 设置运行标志并创建监控线程
        g_runThread = true;
        g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        break;

    case DLL_PROCESS_DETACH:
        // 通知监控线程停止
        if (g_hThread)
        {
            g_runThread = false;
            // 等待线程安全退出（最多等待5秒）
            WaitForSingleObject(g_hThread, 5000);
            // 关闭线程句柄，释放资源
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        // 不做任何事
        break;
    }
    return TRUE;
}
