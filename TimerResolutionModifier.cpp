// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>

#pragma comment(lib, "User32.lib")

// 手动定义 NT_SUCCESS 宏
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

// =======================================================================
//  ↓↓↓ 核心改动：使用函数指针进行动态加载 ↓↓↓
// =======================================================================
// 1. 定义一个与 NtSetTimerResolution 函数签名完全匹配的函数指针类型
typedef NTSTATUS(NTAPI* pNtSetTimerResolution)(ULONG, BOOLEAN, PULONG);

// 2. 创建一个全局的函数指针变量，用于保存获取到的函数地址
pNtSetTimerResolution NtSetTimerResolution_p = nullptr;
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
        // 3. 在调用前，必须检查函数指针是否有效
        if (!NtSetTimerResolution_p) {
            Sleep(1000); // 如果指针无效，则等待并重试，或直接退出
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
                // 4. 通过函数指针来调用函数
                if (NT_SUCCESS(NtSetTimerResolution_p(5000, TRUE, ¤tResolution))) {
                    g_isTimerHigh = true;
                }
            }
        } else {
            if (g_isTimerHigh) {
                if (NT_SUCCESS(NtSetTimerResolution_p(5000, FALSE, ¤tResolution))) {
                    g_isTimerHigh = false;
                }
            }
        }
        Sleep(250);
    }

    if (g_isTimerHigh && NtSetTimerResolution_p)
    {
        NtSetTimerResolution_p(5000, FALSE, ¤tResolution);
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
        //  ↓↓↓ 核心改动：在DLL加载时获取函数地址 ↓↓↓
        // =======================================================================
        // 获取 ntdll.dll 的句柄，它在所有进程中都已加载
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll)
        {
            // 使用 GetProcAddress 获取函数地址，并存入函数指针
            NtSetTimerResolution_p = (pNtSetTimerResolution)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        // =======================================================================

        // 只有成功获取到函数地址后，才创建监控线程
        if (NtSetTimerResolution_p)
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
