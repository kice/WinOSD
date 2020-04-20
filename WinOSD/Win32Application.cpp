#include "Win32Application.h"
#include "LayeredRender.h"

#include "logging.h"
#include "timer.h"

#include <windowsx.h>

#define SHOW_FPS_UPDATE 0

#ifdef _DEBUG
#define SHOW_FPS 1
#else
#define SHOW_FPS 0
#endif

HWND Win32Application::m_hwnd = nullptr;

#define DOWNED 0x8000

static HHOOK mouseHook;
LRESULT CALLBACK MouseHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || !lParam) {
        return CallNextHookEx(mouseHook, nCode, wParam, lParam);
    }

    MSLLHOOKSTRUCT *msll = (MSLLHOOKSTRUCT *)lParam;

    int shifted = GetKeyState(VK_SHIFT) & DOWNED ? MK_SHIFT : 0;
    int ctrled = GetKeyState(VK_CONTROL) & DOWNED ? MK_CONTROL : 0;
    int lbdown = GetKeyState(VK_LBUTTON) & DOWNED ? MK_LBUTTON : 0;
    int mbdown = GetKeyState(VK_LBUTTON) & DOWNED ? MK_MBUTTON : 0;
    int rbdown = GetKeyState(VK_LBUTTON) & DOWNED ? MK_RBUTTON : 0;
    int param = shifted | ctrled | lbdown | mbdown | rbdown;

    switch (wParam) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        PostMessage(Win32Application::GetHwnd(), (UINT)wParam,
                    param, MAKELPARAM(msll->pt.x, msll->pt.y));
        break;
    default:
        break;
    }

    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

int Win32Application::Run(WindowRender *pRenderer, HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = nullptr;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = L"PopupMessage";
    windowClass.hIconSm = nullptr;
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, static_cast<LONG>(pRenderer->GetWidth()), static_cast<LONG>(pRenderer->GetHeight()) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPED | WS_POPUP, FALSE);

    m_hwnd = CreateWindowEx(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        // WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
        windowClass.lpszClassName,
        pRenderer->GetTitle(),
        WS_OVERLAPPED | WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr,
        hInstance, pRenderer);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    pRenderer->OnInit();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    std::thread([] {
#if 0
        return;
#endif // breakpoints will pause this thread, causing mouse to freeze

        mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookCallback, NULL, 0);
        if (!mouseHook) {
            DBG << "Failed to install mouse hook!";
            return;
        }

        MSG msg = { nullptr };
        while (msg.message != WM_QUIT) {
            if (auto ret = GetMessage(&msg, NULL, 0, 0); ret != -1) {
                if (ret == 0) {
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        UnhookWindowsHookEx(mouseHook);
    }).detach();

    MSG msg = { nullptr };
    while (msg.message != WM_QUIT) {
        //// non-blocking message loop
        //if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        //    TranslateMessage(&msg);
        //    DispatchMessage(&msg);
        //}
        //Sleep(100); // or it will take too much cpu time

        // blocking message loop
        if (auto ret = GetMessage(&msg, NULL, 0, 0); ret != -1) {
            if (ret == 0) {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    pRenderer->OnDestroy();
    return static_cast<char>(msg.wParam);
}

LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowRender *pRenderer = reinterpret_cast<WindowRender *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE:
    {
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }
    return 0;

    case WM_TIMER:
        if (pRenderer) {
            pRenderer->OnTimer((int)wParam);
        }
        return 0;

    case WM_KEYDOWN:
        if (pRenderer) {
            pRenderer->OnKeyDown(static_cast<UINT8>(wParam));
        }
        return 0;

    case WM_KEYUP:
        if (pRenderer) {
            pRenderer->OnKeyUp(static_cast<UINT8>(wParam));
        }
        return 0;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        if (pRenderer) {
            bool shifted = wParam & MK_SHIFT;
            bool ctrled = wParam & MK_CONTROL;

            int xPos = (short)LOWORD(lParam);
            int yPos = (short)HIWORD(lParam);

            pRenderer->OnMouse((WindowRender::MouseEvent)message, xPos, yPos, shifted, ctrled);
        }
        return 0;

    case WM_PAINT:
    case WM_DISPLAYCHANGE:
        if (pRenderer) {
#if SHOW_FPS
            static StopWatch update, render;
            double update_ms, render_ms;

            update.start();
            bool redraw = pRenderer->OnUpdate();
            update_ms = update.stop();

            if (redraw) {
                render.start();
                pRenderer->OnRender();
                render_ms = render.stop();

                DBG << "Stats: " << std::setprecision(3)
                    << 1000.f / (update_ms + render_ms) << " fps"
                    << " Update time: " << update_ms << "ms"
                    << " Render time: " << render_ms << "ms";
            } else if (SHOW_FPS_UPDATE) {
                DBG << "Stats: " << std::setprecision(3)
                    << 1000.f / (update_ms) << " fps"
                    << " Update time: " << update_ms << "ms";
            }
#else
            if (pRenderer->OnUpdate()) {
                pRenderer->OnRender();
            }
#endif
        }
        return 0;

    case WM_HOTKEY:
        if (pRenderer) {
            pRenderer->OnHotKey((int)wParam);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
