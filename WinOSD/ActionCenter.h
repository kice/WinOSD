#pragma once

#include <string>
#include <vector>

#include "Image.h"

struct Toast
{
    int priority;
    std::wstring title;
    std::wstring text;
    Image image;
    std::wstring link;
};

class LayeredRender;

class ActionCenter
{
public:
    ActionCenter();
    ~ActionCenter();

    int AddToast(Toast &&toast);

    void SetRender(LayeredRender *render)
    {
        pRender = render;
    }

private:
    LayeredRender *pRender;
};
