// TimerResolutionModifier.cpp
#include <Windows.h>
#include <timeapi.h>
#include <string>
#include <algorithm> // for std::transform

// 链接所需的库
#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "User32.lib")

// 全局变量
static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hThread = NULL;

// 监控线程的主函数 (这部分代码不变)
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

        if (isForeground) {
            if (!g_isTimerHigh) {
                if (timeBeginPeriod(1) == TIMERR_NOERROR) g_isTimerHigh = true;
            }
        } else {
            if (g_isTimerHigh) {
                if (timeEndPeriod(1) == TIMERR_NOERROR) g_isTimerHigh = false;
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

// 导出函数 (不变)
extern "C" __declspec(dllexport) void PlaceholderExport() {}

// DLL入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // =======================================================================
        //  ↓↓↓ 新增的黑名单检查逻辑 ↓↓↓
        // =======================================================================

        // 1. 获取当前进程的可执行文件名
        wchar_t processPath[MAX_PATH];
        GetModuleFileNameW(NULL, processPath, MAX_PATH);
        
        // 从完整路径中提取文件名 (例如 C:\Windows\explorer.exe -> explorer.exe)
        const wchar_t* processName = wcsrchr(processPath, L'\\');
        processName = (processName) ? processName + 1 : processPath;

        // 2. 获取DLL的路径，并构建INI文件的路径
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
        if (lastSlash) {
            *(lastSlash + 1) = L'\0'; // 截断文件名，只保留目录
        }
        std::wstring iniPath = dllPath;
        iniPath += L"blacklist.ini"; // 拼接成完整的INI文件路径

        // 3. 在INI文件中检查进程名是否存在于[Blacklist]区域
        wchar_t valueBuffer[2]; // 只需要一个很小的缓冲区来确认键是否存在
        GetPrivateProfileStringW(
            L"Blacklist",       // Section name
            processName,        // Key name (the process executable)
            L"0",               // Default value if key is not found
            valueBuffer,        // Buffer to receive the value
            sizeof(valueBuffer) / sizeof(wchar_t),
            iniPath.c_str()     // Full path to the INI file
        );

        // 4. 如果读取到的值不是默认值"0"，说明进程在黑名单中
        if (wcscmp(valueBuffer, L"0") != 0)
        {
            // 在黑名单中，立即返回，不执行任何操作
            return TRUE;
        }

        // =======================================================================
        //  ↑↑↑ 黑名单检查逻辑结束 ↑↑↑
        // =======================================================================

        // 如果进程不在黑名单中，则执行原有逻辑
        DisableThreadLibraryCalls(hModule);
        g_runThread = true;
        g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // 清理逻辑 (不变)
        if (g_hThread)
        {
            g_runThread = false;
            WaitForSingleObject(g_hThread, 5000);
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
    }
    return TRUE;
}
