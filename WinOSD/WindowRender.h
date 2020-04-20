#pragma once

#include <windows.h>

#include "Win32Application.h"

#include <string>

class WindowRender
{
public:
    WindowRender(int width, int height, const std::wstring &name) :
        m_width(width),
        m_height(height),
        m_title(name),
        m_useWarpDevice(false)
    {
        m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    }

    virtual ~WindowRender() {}

    virtual void OnInit() = 0;

    // update internal status, return true if require draw new frame
    virtual bool OnUpdate() { return true; };
    virtual void OnRender() = 0;
    virtual void OnDestroy() = 0;

    // Samples override the event handlers to handle specific messages.
    virtual void OnKeyDown(int /*key*/) {}
    virtual void OnKeyUp(int /*key*/) {}
    virtual void OnHotKey(int /*id*/) {}

    virtual void OnTimer(int /*id*/) {}

    enum MouseEvent
    {
        MouseMove = WM_MOUSEMOVE,
        LButtonDown = WM_LBUTTONDOWN,
        LButtonUp = WM_LBUTTONUP,
        RButtonDown = WM_RBUTTONDOWN,
        RButtonUp = WM_RBUTTONUP,
        MButtonDown = WM_MBUTTONDOWN,
        MButtonUp = WM_MBUTTONUP,
    };

    virtual void OnMouse(MouseEvent event, int xPos, int yPos, bool shifted, bool ctrled) {}

    // Accessors.
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    const wchar_t *GetTitle() const { return m_title.c_str(); }

protected:
    void SetCustomWindowText(const std::wstring &text)
    {
        std::wstring windowText = m_title + L": " + text;
        SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
    }

    // Viewport dimensions.
    int m_width;
    int m_height;
    float m_aspectRatio;

    // Adapter info.
    bool m_useWarpDevice;

private:

    // Window title.
    std::wstring m_title;
};
