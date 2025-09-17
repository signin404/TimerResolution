// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string.h> // For _wcsicmp

#pragma comment(lib, "User32.lib")

// 手动定义 NT_SUCCESS 宏
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

// 定义函数指针类型
typedef NTSTATUS(NTAPI* pfnGenericTimerApi)(ULONG, BOOLEAN, PULONG);
typedef DWORD(WINAPI* pfnSetThreadIdealProcessor)(HANDLE, DWORD);

// 全局变量
pfnGenericTimerApi g_pfnSetTimerResolution = nullptr;
pfnSetThreadIdealProcessor g_pfnSetThreadIdealProcessor = nullptr;

static volatile bool g_runThread = false;
static volatile bool g_isTimerHigh = false;
static HANDLE g_hThread = NULL; // 可以是 MonitorThread 或 HookThread 的句柄
static DWORD g_dwThreadId = 0;   // 创建的线程的ID
static ULONG g_targetResolution = 5000;
static bool g_isSpecialProcess = false;
static DWORD g_checkInterval = 10; // 轮询模式的默认间隔（10秒）
static int g_checkMode = 1;        // 1: SetWinEventHook模式, 2: 轮询模式

// 解析亲和性设置字符串 (例如 "7,11,13-15,19")
DWORD_PTR ParseAffinityMask(const std::wstring& affinityStr)
{
    DWORD_PTR mask = 0;
    if (affinityStr.empty()) {
        return 0;
    }

    std::wstringstream ss(affinityStr);
    std::wstring item;

    while (std::getline(ss, item, L',')) {
        size_t hyphenPos = item.find(L'-');
        if (hyphenPos != std::wstring::npos) {
            try {
                int start = std::stoi(item.substr(0, hyphenPos));
                int end = std::stoi(item.substr(hyphenPos + 1));
                if (start >= 0 && end >= start && end < sizeof(DWORD_PTR) * 8) {
                    for (int i = start; i <= end; ++i) {
                        mask |= (static_cast<DWORD_PTR>(1) << i);
                    }
                }
            } catch (...) { /* 忽略无效部分 */ }
        } else {
            try {
                int core = std::stoi(item);
                if (core >= 0 && core < sizeof(DWORD_PTR) * 8) {
                    mask |= (static_cast<DWORD_PTR>(1) << core);
                }
            } catch (...) { /* 忽略无效部分 */ }
        }
    }
    return mask;
}

// WinEventHook 回调函数 (CheckMode=1)
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd) {
        const DWORD currentProcessId = GetCurrentProcessId();
        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(hwnd, &foregroundProcessId);
        ULONG currentResolution;

        if (foregroundProcessId == currentProcessId) {
            if (!g_isTimerHigh) {
                if (g_pfnSetTimerResolution && NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution))) {
                    g_isTimerHigh = true;
                }
            }
        } else {
            if (g_isTimerHigh) {
                if (g_pfnSetTimerResolution && NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution))) {
                    g_isTimerHigh = false;
                }
            }
        }
    }
}

// Hook线程的主函数 (CheckMode=1)
DWORD WINAPI HookThread(LPVOID lpParam)
{
    HWINEVENTHOOK hHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    
    WinEventProc(hHook, EVENT_SYSTEM_FOREGROUND, GetForegroundWindow(), 0, 0, 0, 0);

    MSG msg;
    while (g_runThread && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hHook) {
        UnhookWinEvent(hHook);
    }
    
    if (g_isTimerHigh && g_pfnSetTimerResolution) {
        ULONG currentResolution;
        g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
        g_isTimerHigh = false;
    }

    return 0;
}

// 监控线程的主函数 (轮询模式, CheckMode=2)
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
        Sleep(g_checkInterval * 1000);
    }
    if (g_isTimerHigh && g_pfnSetTimerResolution) {
        ULONG currentResolution;
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

        wchar_t checkBuffer[2]; // Small buffer for checking existence

        // 2. 黑名单检查 (新方法)
        // GetPrivateProfileString will return the default "0" if the key (processName) is not found.
        // If the key exists (e.g., "ChsIME.exe" or "ChsIME.exe=1"), it will return something else.
        GetPrivateProfileStringW(L"BlackList", processName, L"0", checkBuffer, sizeof(checkBuffer), iniPath.c_str());
        if (wcscmp(checkBuffer, L"0") != 0) {
            return TRUE; // In blacklist, do nothing.
        }

        g_targetResolution = GetPrivateProfileIntW(L"Settings", L"Resolution", 5000, iniPath.c_str());
        if (g_targetResolution == 0) { g_targetResolution = 5000; }
        
        g_checkMode = GetPrivateProfileIntW(L"Settings", L"CheckMode", 1, iniPath.c_str());
        g_checkInterval = GetPrivateProfileIntW(L"Settings", L"CheckInterval", 10, iniPath.c_str());
        if (g_checkInterval < 1) { g_checkInterval = 1; }

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        if (!g_pfnSetTimerResolution) { return TRUE; }

        // 5. 检查特殊进程 (新方法)
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

            if (g_checkMode == 2) {
                g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, &g_dwThreadId);
            } else {
                g_hThread = CreateThread(NULL, 0, HookThread, NULL, 0, &g_dwThreadId);
            }

            if (g_hThread != NULL)
            {
                wchar_t affinityBuffer[256];
                GetPrivateProfileStringW(L"Settings", L"Affinity", L"", affinityBuffer, 256, iniPath.c_str());
                std::wstring affinityStr(affinityBuffer);
                if (!affinityStr.empty()) {
                    DWORD_PTR affinityMask = ParseAffinityMask(affinityStr);
                    if (affinityMask > 0) {
                        SetThreadAffinityMask(g_hThread, affinityMask);
                    }
                }

                int idealCore = GetPrivateProfileIntW(L"Settings", L"IdealCore", -1, iniPath.c_str());
                if (idealCore >= 0) 
                {
                    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
                    if (hKernel32) {
                        // =======================================================================
                        // 修正区域: 使用正确的变量来获取和调用 SetThreadIdealProcessor
                        // =======================================================================
                        g_pfnSetThreadIdealProcessor = (pfnSetThreadIdealProcessor)GetProcAddress(hKernel32, "SetThreadIdealProcessor");
                        if (g_pfnSetThreadIdealProcessor) {
                            g_pfnSetThreadIdealProcessor(g_hThread, static_cast<DWORD>(idealCore));
                        }
                    }
                }
            }
        }
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        g_runThread = false; // Signal threads to stop

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
                if (g_checkMode != 2 && g_dwThreadId != 0) {
                    PostThreadMessageW(g_dwThreadId, WM_QUIT, 0, 0);
                }
                WaitForSingleObject(g_hThread, 1000); // Wait briefly for thread to exit
                CloseHandle(g_hThread);
                g_hThread = NULL;
            }
        }
    }
    return TRUE;
}