// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string.h> // For _wcsnicmp

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
static HANDLE g_hThread = NULL;
static DWORD g_dwThreadId = 0;
static ULONG g_targetResolution = 5000;
static bool g_isSpecialProcess = false;
static DWORD g_checkInterval = 10;
static int g_checkMode = 1;

// =======================================================================
// 新增区域: 缓存自身进程ID和事件标志
// =======================================================================
static DWORD g_currentProcessId = 0;
static volatile bool g_foregroundChangeDetected = false;


// 检查进程名是否在INI的某个区域中 (健壮版本)
bool IsProcessInList(const wchar_t* section, const wchar_t* processName, const std::wstring& iniPath)
{
    const DWORD bufferSize = 8192;
    wchar_t buffer[bufferSize];
    DWORD bytesRead = GetPrivateProfileSectionW(section, buffer, bufferSize, iniPath.c_str());
    if (bytesRead == 0) return false;
    size_t processNameLen = wcslen(processName);
    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        const wchar_t* equalsSign = wcschr(p, L'=');
        size_t lenToCompare = (equalsSign != nullptr) ? (equalsSign - p) : wcslen(p);
        if (lenToCompare == processNameLen && _wcsnicmp(p, processName, processNameLen) == 0) {
            return true;
        }
    }
    return false;
}

// 解析亲和性设置字符串
DWORD_PTR ParseAffinityMask(const std::wstring& affinityStr)
{
    DWORD_PTR mask = 0;
    if (affinityStr.empty()) return 0;
    std::wstringstream ss(affinityStr);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        size_t hyphenPos = item.find(L'-');
        if (hyphenPos != std::wstring::npos) {
            try {
                int start = std::stoi(item.substr(0, hyphenPos));
                int end = std::stoi(item.substr(hyphenPos + 1));
                if (start >= 0 && end >= start && end < sizeof(DWORD_PTR) * 8) {
                    for (int i = start; i <= end; ++i) mask |= (static_cast<DWORD_PTR>(1) << i);
                }
            } catch (...) {}
        } else {
            try {
                int core = std::stoi(item);
                if (core >= 0 && core < sizeof(DWORD_PTR) * 8) mask |= (static_cast<DWORD_PTR>(1) << core);
            } catch (...) {}
        }
    }
    return mask;
}

// =======================================================================
// 修改区域: WinEventProc 现在只设置一个标志，不做任何实际的检测
// =======================================================================
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_SYSTEM_FOREGROUND) {
        g_foregroundChangeDetected = true;
    }
}

// =======================================================================
// 修改区域: HookThread 现在是一个结合了事件和轮询的混合模式
// =======================================================================
DWORD WINAPI HookThread(LPVOID lpParam)
{
    HWINEVENTHOOK hHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    
    // 初始检查
    g_foregroundChangeDetected = true;

    while (g_runThread) {
        // 1. 等待 CheckInterval 周期，同时处理消息以确保Hook能触发
        DWORD startTime = GetTickCount();
        while (GetTickCount() - startTime < g_checkInterval * 1000) {
            if (!g_runThread) break; // 允许提前退出
            MSG msg;
            // 处理所有挂起的消息
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Sleep(10); // 短暂休眠以防止CPU空转
        }

        // 2. 周期结束后，如果检测到事件，则执行一次前台检查
        if (g_foregroundChangeDetected) {
            g_foregroundChangeDetected = false; // 重置标志

            HWND hForegroundWnd = GetForegroundWindow();
            DWORD foregroundProcessId = 0;
            if (hForegroundWnd) {
                GetWindowThreadProcessId(hForegroundWnd, &foregroundProcessId);
            }

            ULONG currentResolution;
            if (foregroundProcessId == g_currentProcessId) {
                if (!g_isTimerHigh && g_pfnSetTimerResolution) {
                    if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution))) g_isTimerHigh = true;
                }
            } else {
                if (g_isTimerHigh && g_pfnSetTimerResolution) {
                    if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution))) g_isTimerHigh = false;
                }
            }
        }
    }

    if (hHook) UnhookWinEvent(hHook);
    if (g_isTimerHigh && g_pfnSetTimerResolution) {
        ULONG currentResolution;
        g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
        g_isTimerHigh = false;
    }
    return 0;
}

// 监控线程的主函数 (轮询模式)
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    ULONG currentResolution;
    while (g_runThread) {
        if (!g_pfnSetTimerResolution) { Sleep(1000); continue; }
        HWND hForegroundWnd = GetForegroundWindow();
        DWORD foregroundProcessId = 0;
        if (hForegroundWnd) {
            GetWindowThreadProcessId(hForegroundWnd, &foregroundProcessId);
        }
        
        if (foregroundProcessId == g_currentProcessId) { // 使用缓存的ID
            if (!g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, TRUE, &currentResolution))) g_isTimerHigh = true;
            }
        } else {
            if (g_isTimerHigh) {
                if (NT_SUCCESS(g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution))) g_isTimerHigh = false;
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
        // 缓存自身进程ID
        g_currentProcessId = GetCurrentProcessId();

        wchar_t processPath[MAX_PATH];
        GetModuleFileNameW(NULL, processPath, MAX_PATH);
        const wchar_t* processName = wcsrchr(processPath, L'\\');
        processName = (processName) ? processName + 1 : processPath;
        
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        std::wstring iniPath = dllPath;
        iniPath += L"Config.ini";

        if (IsProcessInList(L"BlackList", processName, iniPath)) {
            return TRUE;
        }

        g_targetResolution = GetPrivateProfileIntW(L"Settings", L"Resolution", 5000, iniPath.c_str());
        if (g_targetResolution == 0) g_targetResolution = 5000;
        
        g_checkMode = GetPrivateProfileIntW(L"Settings", L"CheckMode", 1, iniPath.c_str());
        g_checkInterval = GetPrivateProfileIntW(L"Settings", L"CheckInterval", 10, iniPath.c_str());
        if (g_checkInterval < 1) g_checkInterval = 1;

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            g_pfnSetTimerResolution = (pfnGenericTimerApi)GetProcAddress(hNtdll, "NtSetTimerResolution");
        }
        if (!g_pfnSetTimerResolution) return TRUE;

        // 修改区域: 将 "PersistentProcesses" 改为 "WhiteList"
        if (IsProcessInList(L"WhiteList", processName, iniPath))
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
                if (affinityBuffer[0] != L'\0') {
                    DWORD_PTR affinityMask = ParseAffinityMask(affinityBuffer);
                    if (affinityMask > 0) SetThreadAffinityMask(g_hThread, affinityMask);
                }

                int idealCore = GetPrivateProfileIntW(L"Settings", L"IdealCore", -1, iniPath.c_str());
                if (idealCore >= 0) 
                {
                    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
                    if (hKernel32) {
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
        g_runThread = false;
        if (g_hThread) {
            WaitForSingleObject(g_hThread, 1000);
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
        
        // 确保在DLL卸载时恢复计时器精度
        if (g_isSpecialProcess || g_isTimerHigh) {
             if (g_pfnSetTimerResolution) {
                ULONG currentResolution;
                g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
            }
        }
    }
    return TRUE;
}