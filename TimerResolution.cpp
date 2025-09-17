// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string.h> // For _wcsicmp and _wcsnicmp

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
// 新增区域: 检查进程名是否在INI的某个区域中 (健壮版本)
// 此函数可以正确处理 "process.exe" 和 "process.exe=1" 两种格式
// =======================================================================
bool IsProcessInList(const wchar_t* section, const wchar_t* processName, const std::wstring& iniPath)
{
    const DWORD bufferSize = 8192; // 足够大的缓冲区
    wchar_t buffer[bufferSize];
    
    // 获取区域中的所有条目。返回的格式是 "key1=val1\0key2\0key3=val3\0\0"
    DWORD bytesRead = GetPrivateProfileSectionW(section, buffer, bufferSize, iniPath.c_str());

    if (bytesRead == 0) {
        return false; // 区域为空或不存在
    }

    size_t processNameLen = wcslen(processName);

    // 遍历缓冲区中的每一个以null结尾的字符串
    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        // p 指向当前行, 例如 "MeasureSleep.exe" 或 "MeasureSleep.exe=1"
        
        const wchar_t* equalsSign = wcschr(p, L'=');
        size_t lenToCompare;

        if (equalsSign != nullptr) {
            // 格式为 "key=value", 只比较 key 的部分
            lenToCompare = equalsSign - p;
        } else {
            // 格式为 "key", 比较整个字符串
            lenToCompare = wcslen(p);
        }

        // 使用不区分大小写的比较来匹配进程名
        if (lenToCompare == processNameLen && _wcsnicmp(p, processName, processNameLen) == 0) {
            return true; // 找到匹配项
        }
    }

    return false; // 未找到匹配项
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

// WinEventHook 回调函数
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd) {
        const DWORD currentProcessId = GetCurrentProcessId();
        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(hwnd, &foregroundProcessId);
        ULONG currentResolution;
        if (foregroundProcessId == currentProcessId) {
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

// Hook线程的主函数
DWORD WINAPI HookThread(LPVOID lpParam)
{
    HWINEVENTHOOK hHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    WinEventProc(hHook, EVENT_SYSTEM_FOREGROUND, GetForegroundWindow(), 0, 0, 0, 0);
    MSG msg;
    while (g_runThread && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
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
    const DWORD currentProcessId = GetCurrentProcessId();
    ULONG currentResolution;
    while (g_runThread) {
        if (!g_pfnSetTimerResolution) { Sleep(1000); continue; }
        HWND hForegroundWnd = GetForegroundWindow();
        bool isForeground = false;
        if (hForegroundWnd) {
            DWORD foregroundProcessId = 0;
            GetWindowThreadProcessId(hForegroundWnd, &foregroundProcessId);
            if (foregroundProcessId == currentProcessId) isForeground = true;
        }
        if (isForeground) {
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

        // 2. 黑名单检查 (使用新的健壮函数)
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

        // 5. 检查特殊进程 (使用新的健壮函数)
        if (IsProcessInList(L"PersistentProcesses", processName, iniPath))
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
        if (g_isSpecialProcess) {
            if (g_pfnSetTimerResolution) {
                ULONG currentResolution;
                g_pfnSetTimerResolution(g_targetResolution, FALSE, &currentResolution);
            }
        } else {
            if (g_hThread) {
                if (g_checkMode != 2 && g_dwThreadId != 0) {
                    PostThreadMessageW(g_dwThreadId, WM_QUIT, 0, 0);
                }
                WaitForSingleObject(g_hThread, 1000);
                CloseHandle(g_hThread);
                g_hThread = NULL;
            }
        }
    }
    return TRUE;
}