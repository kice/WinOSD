#include "HttpServer.h"

#include "ActionCenter.h"

#include "Win32Application.h"
#include "LayeredRender.h"

ActionCenter actionCenter;

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetProcessDPIAware();

    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // command line argument handling

    LocalFree(argv);

    RECT screenSize;
    GetWindowRect(GetDesktopWindow(), &screenSize);
    LayeredRender app(screenSize.right, screenSize.bottom, L"PopupMessage");

    actionCenter.SetRender(&app);

    StartHttpServer();

    auto ret = Win32Application::Run(&app, hInstance, nCmdShow);

    StopHttpServer();

    return ret;
}
