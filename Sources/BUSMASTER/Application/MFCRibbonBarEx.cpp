// MFCRibbonBarEx.cpp : implementation file
//

#include "stdafx.h"
#include "BUSMASTER.h"
#include "MFCRibbonBarEx.h"


// CMFCRibbonBarEx

IMPLEMENT_DYNAMIC(CMFCRibbonBarEx, CMFCRibbonBar)

CMFCRibbonBarEx::CMFCRibbonBarEx()
{

}

CMFCRibbonBarEx::~CMFCRibbonBarEx()
{
}


BEGIN_MESSAGE_MAP(CMFCRibbonBarEx, CMFCRibbonBar)
END_MESSAGE_MAP()



// CMFCRibbonBarEx message handlers
bool CMFCRibbonBarEx::isWindowHidden()
{
    return (m_dwHideFlags == AFX_RIBBONBAR_HIDE_ELEMENTS);
}

void CMFCRibbonBarEx::ToggleWindowDisplay()
{
    ToggleMimimizeState();
    UpdateRibbonBarMinimisedIcon();
}

void CMFCRibbonBarEx::UpdateRibbonBarMinimisedIcon()
{
    CMFCRibbonButtonIcon* minimiseBtn = (CMFCRibbonButtonIcon*)FindByID(ID_BUTTON_TOGGLERIBBON);
    if (nullptr != minimiseBtn)
    {
        if (isWindowHidden())
        {
            minimiseBtn->SetIcon(LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_ICON_SHOW_RIBBON)));
        }
        else
        {
            minimiseBtn->SetIcon(LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_ICON_HIDE_RIBBON)));
        }
        minimiseBtn->Redraw();
    }
}
// BUSMASTER-NG: the round application button shows the program icon (the logo of BUSMASTER-NG) instead of the old
// 26 x 26 bitmap of BUSMASTER, drawn at the size of the display scale so that it stays sharp: MFC stretches a
// small image at high scale, but draws an image of 32 pixels or more as it is
void CMFCRibbonBarEx::SetApplicationButtonLogo(UINT unIconId)
{
    CMFCRibbonApplicationButton* pButton = GetApplicationButton();
    if (pButton == nullptr)
    {
        return;
    }
    double dScale = GetGlobalData()->GetRibbonImageScale();
    int nSize = 26;
    if (dScale != 1.)
    {
        nSize = max(32, (int)(26 * dScale + .5));
    }
    HICON hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(unIconId), IMAGE_ICON, nSize, nSize, 0);
    if (hIcon == nullptr)
    {
        return;
    }
    ICONINFO sIconInfo = { 0 };
    if (::GetIconInfo(hIcon, &sIconInfo) && (sIconInfo.hbmColor != nullptr))
    {
        // 32 bit pixels of the icon (straight alpha), premultiplied as MFC draws 32 bit images with AlphaBlend
        BITMAPINFO sInfo = { 0 };
        sInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        sInfo.bmiHeader.biWidth = nSize;
        sInfo.bmiHeader.biHeight = nSize;      // bottom-up
        sInfo.bmiHeader.biPlanes = 1;
        sInfo.bmiHeader.biBitCount = 32;
        sInfo.bmiHeader.biCompression = BI_RGB;
        void* pvBits = nullptr;
        HDC hDC = ::GetDC(nullptr);
        HBITMAP hBitmap = ::CreateDIBSection(hDC, &sInfo, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        if ((hBitmap != nullptr) &&
                (::GetDIBits(hDC, sIconInfo.hbmColor, 0, nSize, pvBits, &sInfo, DIB_RGB_COLORS) == nSize))
        {
            BYTE* pbyPixel = (BYTE*)pvBits;
            for (int nIndex = 0; nIndex < nSize * nSize; nIndex++, pbyPixel += 4)
            {
                pbyPixel[0] = (BYTE)(pbyPixel[0] * pbyPixel[3] / 255);
                pbyPixel[1] = (BYTE)(pbyPixel[1] * pbyPixel[3] / 255);
                pbyPixel[2] = (BYTE)(pbyPixel[2] * pbyPixel[3] / 255);
            }
            pButton->SetImage(hBitmap);    // MFC keeps its own copy
        }
        if (hBitmap != nullptr)
        {
            ::DeleteObject(hBitmap);
        }
        ::ReleaseDC(nullptr, hDC);
    }
    if (sIconInfo.hbmColor != nullptr)
    {
        ::DeleteObject(sIconInfo.hbmColor);
    }
    if (sIconInfo.hbmMask != nullptr)
    {
        ::DeleteObject(sIconInfo.hbmMask);
    }
    ::DestroyIcon(hIcon);
}

BOOL CMFCRibbonBarEx::OnShowRibbonContextMenu(CWnd* pWnd, int x, int y, CMFCRibbonBaseElement* pHit)
{
    auto result = CMFCRibbonBar::OnShowRibbonContextMenu(pWnd, x, y, pHit);
    UpdateRibbonBarMinimisedIcon();
    return result;
}