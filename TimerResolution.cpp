// TimerResolutionModifier.cpp
#include <Windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <tlhelp32.h> // For thread enumeration
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
static HANDLE g_hThread = NULL;
static ULONG g_targetResolution = 5000;
static bool g_isSpecialProcess = false;

// =======================================================================
//  ↓↓↓ 新增：用于MMCSS的全局变量 ↓↓↓
// =======================================================================
static std::vector<HANDLE> g_mmcss_handles; // 存储已注册MMCSS的线程句柄
static std::mutex g_mutex;                  // 用于保护对vector的访问
// =======================================================================


// 监控线程的主函数 (无任何改动)
DWORD WINAPI MonitorThread(LPVOID lpParam) { /* ... (此函数代码与上一版完全相同) ... */ }

// =======================================================================
//  ↓↓↓ 新增：用于注册和注销MMCSS的工作函数 ↓↓↓
// =======================================================================
// 在一个新线程中执行，以避免在DllMain中做过多工作
DWORD WINAPI MmcssWorker(LPVOID lpParam)
{
    wchar_t* taskName = (wchar_t*)lpParam;
    DWORD taskIndex = 0;

    // 1. 枚举当前进程的所有线程
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        delete[] taskName; // 清理内存
        return 1;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);
    DWORD currentProcessId = GetCurrentProcessId();

    if (Thread32First(hSnap, &te32)) {
        do {
            // 2. 检查线程是否属于当前进程
            if (te32.th32OwnerProcessID == currentProcessId) {
                // 3. 尝试为该线程注册MMCSS
                HANDLE hThread = OpenThread(THREAD_SET_INFORMATION, FALSE, te32.th32ThreadID);
                if (hThread) {
                    HANDLE hMmcss = AvSetMmThreadCharacteristicsW(hThread, taskName, &taskIndex);
                    if (hMmcss) {
                        // 4. 如果成功，保存句柄以便将来清理
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_mmcss_handles.push_back(hMmcss);
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnap, &te32));
    }

    CloseHandle(hSnap);
    delete[] taskName; // 清理从DllMain传递过来的字符串内存
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

        // 5. 检查是否为特殊进程 (永久修改计时器精度)
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
            g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);

            // =======================================================================
            //  ↓↓↓ 新增：为普通进程启用MMCSS调度 ↓↓↓
            // =======================================================================
            if (GetPrivateProfileIntW(L"MMCSS", L"Enabled", 0, iniPath.c_str()) == 1)
            {
                wchar_t* taskName = new wchar_t[64];
                GetPrivateProfileStringW(L"MMCSS", L"TaskName", L"Pro Audio", taskName, 64, iniPath.c_str());
                // 创建一个新线程来执行MMCSS注册，避免阻塞DllMain
                CreateThread(NULL, 0, MmcssWorker, taskName, 0, NULL);
            }
            // =======================================================================
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
            if (g_hThread)
            {
                g_runThread = false;
                WaitForSingleObject(g_hThread, 5000);
                CloseHandle(g_hThread);
                g_hThread = NULL;
            }
        }

        // =======================================================================
        //  ↓↓↓ 新增：清理所有已注册的MMCSS句柄 ↓↓↓
        // =======================================================================
        std::lock_guard<std::mutex> lock(g_mutex);
        for (HANDLE hMmcss : g_mmcss_handles) {
            AvRevertMmThreadCharacteristics(hMmcss);
        }
        g_mmcss_handles.clear();
        // =======================================================================
    }
    return TRUE;
}