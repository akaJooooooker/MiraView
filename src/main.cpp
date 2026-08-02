#include "App.h"

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::filesystem::path initialFile;
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments && argumentCount > 1) initialFile = arguments[1];
    if (arguments) LocalFree(arguments);

    App app;
    const int result = app.Run(instance, initialFile);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}

