// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>

#pragma comment(lib, "User32.lib")

// 手动定义 NT_SUCCESS 宏
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

// =======================================================================
//  ↓↓↓ 终极解决方案：使用完全通用的名称来定义函数指针 ↓↓↓
// =======================================================================
// 1. 定义一个通用的函数指针类型
typedef NTSTATUS(NTAPI* pfnGenericTimerApi)(ULONG, BOOLEAN, PULONG);

// 2. 创建一个全局的、使用通用名称的函数指针变量
pfnGenericTimerApi g_pfnSetTimerResolution = nullptr;
// =======================================================================

// 全局变量
static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hThread = NULL;

// 监控线程的主函数
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    const DWORD currentProcessId = GetCurrentProcessId();
    ULONG currentResolution;

    while (g_runThread)
    {
        // 3. 检查通用函数指针是否有效
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
                // 4. 通过通用函数指针来调用函数
                if (NT_SUCCESS(g_pfnSetTimerResolution(5000, TRUE, ¤tResolution))) {
                    g_isTimerHigh = true;
                }
            }
        } else {
            if (g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(5000, FALSE, ¤tResolution))) {
                    g_isTimerHigh = false;
                }
            }
        }
        Sleep(250);
    }

    if (g_isTimerHigh && g_pfnSetTimerResolution)
    {
        g_pfnSetTimerResolution(5000, FALSE, ¤tResolution);
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
        // 黑名单检查 (保持不变)
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
        wchar_t valueBuffer[2];
        GetPrivateProfileStringW(L"Blacklist", processName, L"0", valueBuffer, sizeof(valueBuffer) / sizeof(wchar_t), iniPath.c_str());
        if (wcscmp(valueBuffer, L"0") != 0) { return TRUE; }

        // =======================================================================
        //  ↓↓↓ 使用通用名称的指针来获取函数地址 ↓↓↓
        // =======================================================================
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll)
        {
            // GetProcAddress 使用的是纯字符串，这部分不会有问题
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        // =======================================================================

        if (g_pfnSetTimerResolution)
        {
            DisableThreadLibraryCalls(hModule);
            g_runThread = true;
            g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        }
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
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
