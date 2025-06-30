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

// =======================================================================
//  ↓↓↓ 新增的全局变量，用于保存从INI读取的精度值 ↓↓↓
// =======================================================================
static ULONG g_targetResolution = 5000; // 默认值为 0.5ms (5000 * 100ns)
// =======================================================================


// 监控线程的主函数
DWORD WINAPI MonitorThread(LPVOID lpParam)
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
                // =======================================================================
                //  ↓↓↓ 修改：使用配置好的精度值 ↓↓↓
                // =======================================================================
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
        //  ↓↓↓ 新增：在黑名单检查后，读取INI配置，仅执行一次 ↓↓↓
        // =======================================================================
        // 使用 GetPrivateProfileIntW 读取整数值，如果键不存在，则使用第三个参数作为默认值
        g_targetResolution = GetPrivateProfileIntW(
            L"Settings",          // Section name
            L"Resolution",        // Key name
            5000,                 // Default value (0.5ms)
            iniPath.c_str()       // Full path to the INI file
        );

        // 做一个简单的安全检查，防止用户输入0
        if (g_targetResolution == 0) {
            g_targetResolution = 5000;
        }
        // =======================================================================

        // 获取函数地址 (保持不变)
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll)
        {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }

        // 启动线程 (保持不变)
        if (g_pfnSetTimerResolution)
        {
            DisableThreadLibraryCalls(hModule);
            g_runThread = true;
            g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        }
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // 清理逻辑 (保持不变)
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