#include "ActionCenter.h"
#include "LayeredRender.h"

#include "logging.h"
#include "strings.h"

Image DownloadImage(const std::wstring &url);

ActionCenter::ActionCenter()
{}

int ActionCenter::AddToast(Toast &&toast)
{
    if (!pRender) {
        return -1;
    }

    float maxWidth = pRender->GetDrawableWidth();
    if (toast.image.width > maxWidth) {
        int w = (int)maxWidth;
        int h = (int)(toast.image.height * maxWidth / toast.image.width);

        toast.image = toast.image.resize(w, h);
    }

    auto toastId = pRender->AddToast(toast.title, toast.text, toast.image, toast.link);
    if (toastId > 0) {
        pRender->SetTopMost(3.f);
    }

    return toastId;
}

ActionCenter::~ActionCenter()
{}
