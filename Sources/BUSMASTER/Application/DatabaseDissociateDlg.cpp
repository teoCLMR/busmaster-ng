/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      DatabaseDissociateDlg.cpp
 * \brief     Contains the description of the class CDatabaseDissociateDlg
 * \author    Ratnadip Choudhury
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Contains the description of the class CDatabaseDissociateDlg
 */
#include "stdafx.h"
#include "BUSMASTER.h"
#include "DatabaseDissociateDlg.h"
#include "MainFrm.h"

extern CCANMonitorApp theApp;

// CDatabaseDissociateDlg dialog

IMPLEMENT_DYNAMIC(CDatabaseDissociateDlg, CDialog)
CDatabaseDissociateDlg::CDatabaseDissociateDlg( std::list<std::string> dbPath, CWnd* pParent )
    : CDialog(CDatabaseDissociateDlg::IDD, pParent)
{
    mDbPaths = dbPath;
}

CDatabaseDissociateDlg::~CDatabaseDissociateDlg()
{
}

void CDatabaseDissociateDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LSTB_DISSOCIATE_DBNAMES, m_omDissociateDbLst);
}


BEGIN_MESSAGE_MAP(CDatabaseDissociateDlg, CDialog)
    ON_BN_CLICKED(IDC_CBTN_DISSOCIATE, OnBnClickedCbtnDissociate)
    ON_LBN_SELCHANGE(IDC_LSTB_DISSOCIATE_DBNAMES, OnlbnSelChangeDbList)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()


/******************************************************************************
  Function Name    :  OnInitDialog

  Input(s)         :
  Output           :
  Functionality    :  Initialze the contents of List box
  Member of        :  CDatabaseDissociateDlg
  Friend of        :      -

  Author(s)        :  Anish Kumar
  Date Created     :  06.12.2006
  Modifications    :
******************************************************************************/
BOOL CDatabaseDissociateDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    //do initialization below
    m_omDissociateDbLst.ResetContent();
    mDbPathsDissociated.clear();
    CSize   sz;
    CDC*  pDC = m_omDissociateDbLst.GetDC();
    // BUSMASTER-NG: measure with the font of the list box. The DC of the list box has the system font selected,
    // which is narrower, so the last characters of long paths could not be scrolled to.
    CFont* pOldFont = pDC->SelectObject(m_omDissociateDbLst.GetFont());
    int nDx = 0;
for ( auto path : mDbPaths )
    {
        m_omDissociateDbLst.AddString( path.c_str() );
        sz = pDC->GetTextExtent( path.c_str() );

        if ( sz.cx > nDx )
        {
            nDx = sz.cx;
        }
    }
    nDx += pDC->GetTextExtent("W").cx;     // room after the last character
    pDC->SelectObject(pOldFont);

    m_omDissociateDbLst.ReleaseDC(pDC);
    // Set the horizontal extent so every character of all strings
    // can be scrolled to.
    m_omDissociateDbLst.SetHorizontalExtent(nDx);

    // BUSMASTER-NG: the dialog can be resized; it opens wide enough for the longest path (up to 3/4 of the screen)
    CRect omClient;
    GetClientRect(&omClient);
    CRect omList;
    m_omDissociateDbLst.GetWindowRect(&omList);
    ScreenToClient(&omList);
    m_omListMargins = CRect(omList.left, omList.top, omClient.right - omList.right, omClient.bottom - omList.bottom);
    CRect omButton;
    GetDlgItem(IDC_CBTN_DISSOCIATE)->GetWindowRect(&omButton);
    ScreenToClient(&omButton);
    m_omDissociateFromCorner = CSize(omClient.right - omButton.left, omClient.bottom - omButton.top);
    GetDlgItem(IDOK)->GetWindowRect(&omButton);
    ScreenToClient(&omButton);
    m_omOkFromCorner = CSize(omClient.right - omButton.left, omClient.bottom - omButton.top);
    CRect omWindow;
    GetWindowRect(&omWindow);
    m_omMinSize = omWindow.Size();
    m_bLayoutReady = true;

    int nWanted = nDx + m_omListMargins.left + m_omListMargins.right + ::GetSystemMetrics(SM_CXVSCROLL)
                  + (omWindow.Width() - omClient.Width()) + 8;
    int nWidth = max(omWindow.Width(), min(nWanted, ::GetSystemMetrics(SM_CXSCREEN) * 3 / 4));
    SetWindowPos(nullptr, 0, 0, nWidth, omWindow.Height() * 3 / 2, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();
    return TRUE;
}

void CDatabaseDissociateDlg::vArrangeControls()
{
    CRect omClient;
    GetClientRect(&omClient);
    m_omDissociateDbLst.MoveWindow(m_omListMargins.left, m_omListMargins.top,
                                   omClient.Width() - m_omListMargins.left - m_omListMargins.right,
                                   omClient.Height() - m_omListMargins.top - m_omListMargins.bottom);
    CWnd* pButtons[] = { GetDlgItem(IDC_CBTN_DISSOCIATE), GetDlgItem(IDOK) };
    CSize omOffsets[] = { m_omDissociateFromCorner, m_omOkFromCorner };
    for (int nIndex = 0; nIndex < 2; nIndex++)
    {
        if (nullptr != pButtons[nIndex])
        {
            pButtons[nIndex]->SetWindowPos(nullptr, omClient.right - omOffsets[nIndex].cx, omClient.bottom - omOffsets[nIndex].cy,
                                           0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
    Invalidate();
}

void CDatabaseDissociateDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    if (m_bLayoutReady && (SIZE_MINIMIZED != nType))
    {
        vArrangeControls();
    }
}

void CDatabaseDissociateDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CDialog::OnGetMinMaxInfo(lpMMI);
    if (m_bLayoutReady)
    {
        lpMMI->ptMinTrackSize.x = m_omMinSize.cx;
        lpMMI->ptMinTrackSize.y = m_omMinSize.cy;
    }
}

/******************************************************************************
  Function Name    :  OnBnClickedCbtnDissociate

  Input(s)         :
  Output           :
  Functionality    :  Call the functions to remove the selected Databases
  Member of        :  CDatabaseDissociateDlg
  Friend of        :      -

  Author(s)        :  Anish Kumar
  Date Created     :  06.12.2006
  Modifications    :
******************************************************************************/
void CDatabaseDissociateDlg::OnBnClickedCbtnDissociate()
{
    int nCount = m_omDissociateDbLst.GetSelCount();
    if (nCount > 0)
    {
        if (IDOK == AfxMessageBox(_(defDISSOCIATE_WARNING), MB_OKCANCEL | MB_ICONWARNING))
        {
            // Array of selected item's position
            CArray<int, int> aomListBoxSel;
            aomListBoxSel.SetSize(nCount);
            //Pass the array pointer to get the selected item's positions
            m_omDissociateDbLst.GetSelItems(nCount, aomListBoxSel.GetData());
            int nSelectedPos = 0;
            for (int nTempCnt = 0; nTempCnt < nCount; nTempCnt++)
            {
                CString omstrDBPath;
                //Selected file's index
                nSelectedPos = aomListBoxSel.GetAt(nTempCnt);
                //Find the length of string to pass the buffer to have the selected File path
                int nBufferSize = m_omDissociateDbLst.GetTextLen(nSelectedPos);
                m_omDissociateDbLst.GetText(nSelectedPos, omstrDBPath.GetBuffer(nBufferSize));
                mDbPathsDissociated.push_back(omstrDBPath.GetBuffer(0));
            }
            for (int nTempCnt = 0; nTempCnt < nCount; nTempCnt++)
            {
                nSelectedPos = aomListBoxSel.GetAt(nTempCnt) - nTempCnt;
                m_omDissociateDbLst.DeleteString(nSelectedPos);
            }

            CButton* pButton = (CButton*)GetDlgItem(IDC_CBTN_DISSOCIATE);
            if (nullptr != pButton)
            {
                pButton->EnableWindow(FALSE);
            }
        }
    }


}
void CDatabaseDissociateDlg::OnOK()
{
    if (mDbPathsDissociated.size() > 0)
    {
        CDialog::OnOK();
    }
    else
    {
        CDialog::OnCancel();
    }
}
void CDatabaseDissociateDlg::OnClose()
{
    OnOK();
}
std::list<std::string> CDatabaseDissociateDlg::GetDissociatedFiles()
{
    return mDbPathsDissociated;
}
void CDatabaseDissociateDlg::OnlbnSelChangeDbList()
{
    CButton* pButton = (CButton*)GetDlgItem(IDC_CBTN_DISSOCIATE);
    if (nullptr != pButton)
    {
        if (m_omDissociateDbLst.GetSelCount() > 0)
        {
            pButton->EnableWindow(TRUE);
        }
        else
        {
            pButton->EnableWindow(FALSE);
        }
    }

}
