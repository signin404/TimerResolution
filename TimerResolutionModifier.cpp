// TimerResolutionModifier.cpp
#include <Windows.h>
#include <timeapi.h>

#pragma comment(lib, "Winmm.lib")

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        timeBeginPeriod(1);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        timeEndPeriod(1);
        break;
    }
    return TRUE;
}
