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

// =======================================================================
//  ↓↓↓ 新增/修改：用于MMCSS的全局变量 ↓↓↓
// =======================================================================
static HANDLE g_hMmcssThread = NULL;     // MMCSS专用线程的句柄
static HANDLE g_hShutdownEvent = NULL;   // 用于通知线程退出的事件
// =======================================================================

// 计时器精度监控线程 (此函数无任何改动)
DWORD WINAPI TimerMonitorThread(LPVOID lpParam) { /* ... (此函数代码与上一版完全相同) ... */ }

// =======================================================================
//  ↓↓↓ 新增：MMCSS专用线程的主函数 ↓↓↓
// =======================================================================
DWORD WINAPI MmcssRegistrationThread(LPVOID lpParam)
{
    wchar_t* taskName = (wchar_t*)lpParam;
    DWORD taskIndex = 0;
    HANDLE hMmcssTask = NULL;

    // 1. 调用函数，将当前线程（自己）注册到MMCSS
    hMmcssTask = AvSetMmThreadCharacteristicsW(taskName, &taskIndex);

    // 释放传递过来的字符串内存
    delete[] taskName;

    if (hMmcssTask)
    {
        // 2. 注册成功，进入等待状态，直到收到关闭信号
        WaitForSingleObject(g_hShutdownEvent, INFINITE);

        // 3. 收到信号后，清理并恢复线程特性
        AvRevertMmThreadCharacteristics(hMmcssTask);
    }

    return 0;
}
// =======================================================================

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
        iniPath += L"blacklist.ini";

        // 2. 黑名单检查
        wchar_t checkBuffer[2];
        GetPrivateProfileStringW(L"Blacklist", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0) { return TRUE; }

        // 3. 读取计时器精度配置
        g_targetResolution = GetPrivateProfileIntW(L"Settings", L"Resolution", 5000, iniPath.c_str());
        if (g_targetResolution == 0) { g_targetResolution = 5000; }

        // 4. 获取函数地址
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        if (!g_pfnSetTimerResolution) { return TRUE; }

        // 5. 检查是否为特殊进程
        GetPrivateProfileStringW(L"PersistentProcesses", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0)
        {
            g_isSpecialProcess = true;
            ULONG currentResolution;
            g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution);
        }
        else
        {
            // 6. 对于普通进程，启动计时器监控线程
            g_isSpecialProcess = false;
            DisableThreadLibraryCalls(hModule);
            g_runThread = true;
            g_hTimerThread = CreateThread(NULL, 0, TimerMonitorThread, NULL, 0, NULL);

            // 7. 为普通进程启用MMCSS调度
            if (GetPrivateProfileIntW(L"MMCSS", L"Enabled", 0, iniPath.c_str()) == 1)
            {
                // 创建一个事件用于未来的关闭信号
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

        // =======================================================================
        //  ↓↓↓ 新增：清理MMCSS线程和相关句柄 ↓↓↓
        // =======================================================================
        if (g_hMmcssThread) {
            // 发送关闭信号
            if (g_hShutdownEvent) SetEvent(g_hShutdownEvent);
            // 等待线程结束
            WaitForSingleObject(g_hMmcssThread, 5000);
            // 清理句柄
            CloseHandle(g_hMmcssThread);
            if (g_hShutdownEvent) CloseHandle(g_hShutdownEvent);
        }
        // =======================================================================
    }
    return TRUE;
}