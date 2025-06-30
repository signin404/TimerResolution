// TimerResolutionModifier.cpp
#include <Windows.h>
#include <timeapi.h>

#pragma comment(lib, "Winmm.lib")

// =======================================================================
//  ↓↓↓ 新增的导出函数 ↓↓↓
// =======================================================================
//
// 这个函数是空的，什么也不做。
// 它的唯一目的就是被“导出”，以满足那些要求DLL至少有一个
// 导出函数的注入器。
// extern "C" 确保函数名不会被C++编译器修改。
// __declspec(dllexport) 告诉编译器将此函数放入DLL的导出表中。
//
extern "C" __declspec(dllexport) void PlaceholderExport()
{
    // Do nothing.
}
// =======================================================================


// DllMain 是 DLL 的入口点函数
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        // DLL被注入到进程时，请求1ms的计时器精度
        timeBeginPeriod(1);
        break;

    case DLL_THREAD_ATTACH:
        // 进程创建新线程时执行，我们此处不需要操作
        break;

    case DLL_THREAD_DETACH:
        // 线程退出时执行，我们此处不需要操作
        break;

    case DLL_PROCESS_DETACH:
        // DLL从进程卸载时，释放计时器精度请求
        timeEndPeriod(1);
        break;
    }
    return TRUE; // 操作成功
}
