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
 * \file      SignalDetailsDlg.cpp
 * \brief     This file contain definition of all function of
 * \author    Amarnath Shastry
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * This file contain definition of all function of
 */
#include "stdafx.h"             // Standard header
#include "BUSMASTER.h"        // App class header
#include <math.h>               // for pow(...)
#include "DataType.h"           // For max DLC CAN
#include "SignalDetailsDlg.h"   // for creating and editing signal details
#include "MainFrm.h"            // Hex validation is defined here
#include "Utility/UtilFunctions.h"      // For Utility Functions
#include "Utility/DbfNumber.h"
#include ".\signaldetailsdlg.h"

extern CCANMonitorApp theApp;
/////////////////////////////////////////////////////////////////////////////
// CSignalDetailsDlg dialog

/*******************************************************************************
 Function Name    :  CSignalDetailsDlg
 Input(s)         :  eMODES eMode,
                     int nDataFormat,
                     CString omStrMsgName,
                     UINT nMsgLen,
                     CString omStrSignalType
 Output           :
 Functionality    :  Constructor
 Member of        :  CSignalDetailsDlg
 Friend of        :      -
 Author(s)        :  Amarnath Shastry
 Date Created     :  21.02.2002
 Modifications    :
/******************************************************************************/
CSignalDetailsDlg::CSignalDetailsDlg(const SDBPARAMS& sDbParams,
                                     eMODES eMode,
                                     int nDataFormat,
                                     CString omStrMsgName,
                                     UINT nMsgLen,
                                     CString omStrSignalType/*""*/,
                                     CString omStrMaxVal/*""*/,
                                     CString omStrMinVal/*""*/,
                                     CString omStrOffset/*""*/,
                                     CString omStrScale/*""*/,
                                     CWnd* pParent /*=nullptr*/)
    : CDialog(CSignalDetailsDlg::IDD, pParent)
{
    //{{AFX_DATA_INIT(CSignalDetailsDlg)
    m_unMinVal = 1;
    m_shByteIndex = 0;
    m_unSgLen = 1;
    m_omStrSignalName = "";
    m_byStartBit = 0;
    m_omStrUnit = "";
    m_nMsgLength = nMsgLen;
    m_omStrMsgName = omStrMsgName;
    m_bIsCanceled = FALSE;
    m_bIsDataSaved = TRUE;
    m_omStrPrevSignalName = "";
    m_unMode = eMode;
    m_nDataFormat = nDataFormat;
    m_omStrSgType = omStrSignalType;
    //}}AFX_DATA_INIT

    m_omStrMaxVal = omStrMaxVal;
    m_omStrMinVal = omStrMinVal;
    m_omStrOffset = omStrOffset;
    m_omStrScale  = omStrScale;
    m_bDupliacateFound = FALSE;
    m_bLenChanged = FALSE;
    m_bNameChanged = FALSE;
    m_sDbParams = sDbParams;
    m_bUpdatingPhysical = true;     // until OnInitDialog has filled the controls
}

/*******************************************************************************
 Function Name    :  CSignalDetailsDlg
 Input(s)         :  eMode - Mode of the View
                     psSigInfo - Pointer to the Signal Information
                     pParent   - Pointer to the Parent Window
 Output           :   -
 Functionality    :  Constructor
 Member of        :  CSignalDetailsDlg
 Friend of        :      -
 Author(s)        :  Raja N
 Date Created     :  22.07.2004
 Modifications    :  Raja N on 31.07.2004, Modifications as per code review
                     comments and changed code with utility class function calls
/******************************************************************************/
CSignalDetailsDlg::CSignalDetailsDlg( eMODES eMode,
                                      sSIGNALS* psSigInfo,
                                      CWnd* pParent /*=nullptr*/)
    : CDialog(CSignalDetailsDlg::IDD, pParent)
{
    m_unMinVal = 1;
    m_shByteIndex = 0;
    m_unSgLen = 1;
    m_omStrSignalName = "";
    m_byStartBit = 0;
    m_omStrUnit = "";
    m_nMsgLength = 0;
    m_omStrMsgName = "";
    m_bIsCanceled = FALSE;
    m_bIsDataSaved = TRUE;
    m_omStrPrevSignalName = "";
    m_unMode = eMode;
    m_nDataFormat = eEndianess::eIntel;
    m_omStrSgType = "";
    //}}AFX_DATA_INIT

    m_omStrMaxVal = "";
    m_omStrMinVal = "";
    m_omStrOffset = "";
    m_omStrScale  = "";
    m_bDupliacateFound = FALSE;
    m_bLenChanged = FALSE;
    m_bNameChanged = FALSE;


    m_bUpdatingPhysical = true;     // until OnInitDialog has filled the controls

    if( psSigInfo != nullptr)
    {
        if( eMode == MD_READ_ONLY )
        {
            m_shByteIndex = (SHORT)psSigInfo->m_unStartByte-1;
            m_unSgLen = psSigInfo->m_unSignalLength;
            m_omStrSignalName = psSigInfo->m_omStrSignalName;
            m_byStartBit = psSigInfo->m_byStartBit;
            m_omStrUnit = psSigInfo->m_omStrSignalUnit;
            m_nMsgLength = psSigInfo->m_unSignalLength;
            m_bIsCanceled = FALSE;
            m_bIsDataSaved = TRUE;
            m_unMode = eMode;
            m_nDataFormat = psSigInfo->m_eFormat;
            m_omStrSgType = CString((wchar_t)(psSigInfo->m_bySignalType));

            // Format the min value
            __int64 unVal = psSigInfo->m_SignalMaxValue.n64Value;
            CUtilFunctions::s_vRemoveUnwantedBits( unVal,
                                                   psSigInfo->m_unSignalLength);
            m_omStrMaxVal.Format(defFORMAT_INT64_HEX, unVal);

            unVal = psSigInfo->m_SignalMinValue.n64Value;
            CUtilFunctions::s_vRemoveUnwantedBits( unVal,
                                                   psSigInfo->m_unSignalLength);
            m_omStrMinVal.Format(defFORMAT_INT64_HEX, unVal);
            // Format the factor value
            m_omStrScale = DbfNumber::strFormat(psSigInfo->m_fSignalFactor).c_str();
            // Foramt the offset value
            m_omStrOffset = DbfNumber::strFormat(psSigInfo->m_fSignalOffset).c_str();
            m_bDupliacateFound = FALSE;
            m_bLenChanged = FALSE;
            m_bNameChanged = FALSE;
        }
        else
        {
            // Implementation should be done for other options
            // Don't use before that
            ASSERT( FALSE );
        }
    }
    else
    {
        ASSERT( FALSE );
    }
}

void CSignalDetailsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CSignalDetailsDlg)
    DDX_Control(pDX, IDC_EDIT_FACTOR, m_odScale);
    DDX_Control(pDX, IDC_EDIT_OFFSET, m_odOffset);
    DDX_Control(pDX, IDC_EDIT_MIN, m_odMinValue);
    DDX_Control(pDX, IDC_EDIT_MAX, m_odMaxValue);
    DDX_Control(pDX, IDC_SPIN_SGLENGTH, m_omSpinLen);
    DDX_Control(pDX, IDC_SPIN_BYINDEX, m_omSpinByIndex);
    DDX_Control(pDX, IDC_SPIN_BIT, m_omSpinStartBit);
    DDX_Control(pDX, IDC_COMB_SGTYPE, m_omComboSgType);
    DDX_Text(pDX, IDC_EDIT_BYINDEX, m_shByteIndex);
    DDV_MinMaxShort(pDX, m_shByteIndex, 0, (SHORT)m_nMsgLength-1);          //VENKAT
    DDX_Text(pDX, IDC_EDIT_SGLEN, m_unSgLen);
    DDV_MinMaxUInt(pDX, m_unSgLen, m_unMinVal, min (64, m_nMsgLength*8));
    DDX_Text(pDX, IDC_EDIT_SGNAME, m_omStrSignalName);
    DDX_Text(pDX, IDC_EDIT_STBIT, m_byStartBit);
    DDV_MinMaxByte(pDX, m_byStartBit, 0, 7);
    DDX_Text(pDX, IDC_EDIT_UNIT, m_omStrUnit);
    //}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CSignalDetailsDlg, CDialog)
    //{{AFX_MSG_MAP(CSignalDetailsDlg)
    ON_EN_KILLFOCUS(IDC_EDIT_SGNAME, OnKillfocusEditSgname)
    ON_EN_KILLFOCUS(IDC_EDIT_BYINDEX, OnKillfocusEditByindex)
    ON_EN_KILLFOCUS(IDC_EDIT_FACTOR, OnKillfocusEditFactor)
    ON_EN_KILLFOCUS(IDC_EDIT_OFFSET, OnKillfocusEditOffset)
    ON_EN_KILLFOCUS(IDC_EDIT_SGLEN, OnKillfocusEditSglen)
    ON_EN_KILLFOCUS(IDC_EDIT_STBIT, OnKillfocusEditStbit)
    ON_EN_KILLFOCUS(IDC_EDIT_UNIT, OnKillfocusEditUnit)
    ON_CBN_SELCHANGE(IDC_COMB_SGTYPE, OnSelchangeCombSgtype)
    ON_EN_CHANGE(IDC_EDIT_SGLEN, OnChangeEditSglen)
    ON_EN_CHANGE(IDC_EDIT_SGNAME, OnChangeEditSgname)
    //}}AFX_MSG_MAP
    ON_BN_CLICKED(IDC_RADIO_INTEL, OnBnClickedRadioIntel)
    ON_BN_CLICKED(IDC_RADIO_MOTOROLA, OnBnClickedRadioMotorola)
    // (CRadixEdit keeps EN_CHANGE for itself: the physical range follows on kill focus)
    ON_EN_KILLFOCUS(IDC_EDIT_MIN, OnChangeRawOrScaling)
    ON_EN_KILLFOCUS(IDC_EDIT_MAX, OnChangeRawOrScaling)
    ON_EN_CHANGE(IDC_EDIT_BYINDEX, OnChangeBitPosition)
    ON_EN_CHANGE(IDC_EDIT_STBIT, OnChangeBitPosition)
    ON_EN_KILLFOCUS(IDC_EDIT_PHYS_MIN, OnKillfocusEditPhysMin)
    ON_EN_KILLFOCUS(IDC_EDIT_PHYS_MAX, OnKillfocusEditPhysMax)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CSignalDetailsDlg message handlers
/******************************************************************************/
/*  Function Name    :  OnInitDialog                                          */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Fills the combo box control,
                        sets the range for spin controls
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  21.02.2002                                            */
/*  Modifications    :  Rajesh Kumar 04.03.2003                               */
/*                      modified w.r.t changed min max types                  */
/*. Modifications    :  Amitesh Bharti, 04.07.2003,                           */
/*                        Signal length spin control will have maximum value  */
/*                        as message length * 8                               */
/*  Modifications    :  Raja N on 10.03.2004                                  */
/*                      Initialised the variable to remember the previous     */
/*                      state to avoide overwrite of signal length while      */
/*                      selecing the same type again from signal detalis      */
/*  Modifications    :  Raja N on 10.03.2004                                  */
/*                      Modified init to have a read only view of the dialog  */
/*  Modifications    :  Raja N on 22.07.2004                                  */
/*                      Fixed the negative value problem for HEX number       */
/*  Modifications    :  Raja N on 31.07.2004                                  */
/*                      Implemented code review comments to check pointers    */
/******************************************************************************/

BOOL CSignalDetailsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    if(theApp.pouGetFlagsPtr()->nGetFlagStatus(HEX))
    {
        m_odMaxValue.vSetBase(BASE_HEXADECIMAL);
        m_odMinValue.vSetBase(BASE_HEXADECIMAL);

        CStatic* pStatic = (CStatic*)GetDlgItem(IDC_STAT_MAX);
        pStatic->SetWindowText("Max Val: 0x");

        pStatic = (CStatic*)GetDlgItem(IDC_STAT_MIN);
        pStatic->SetWindowText("Min Val: 0x");
    }
    else
    {
        m_odMaxValue.vSetBase(BASE_DECIMAL);
        m_odMinValue.vSetBase(BASE_DECIMAL);

        CStatic* pStatic = (CStatic*)GetDlgItem(IDC_STAT_MAX);
        pStatic->SetWindowText("Max Val:");

        pStatic = (CStatic*)GetDlgItem(IDC_STAT_MIN);
        pStatic->SetWindowText("Min Val:");
    }

    m_odNumericEdit.SubclassDlgItem(IDC_EDIT_SGNAME,this);
    SIG_VALUE minVal;
    SIG_VALUE maxVal;
    CMainFrame* pMainFrame = static_cast<CMainFrame*> (AfxGetApp()->m_pMainWnd);

    // Add options to the combo
    m_omComboSgType.AddString( defBOOLEAN );
    m_omComboSgType.AddString( defUNSIGNED_INT );
    m_omComboSgType.AddString( defSIGNED_INT );

    // Set range for byte index, signal length and bit index
    UINT unMaxSignalLength = static_cast <UINT> (m_nMsgLength * 8) ;
    m_omSpinLen.SetRange( 1, (short)min (64, unMaxSignalLength) );
    m_omSpinByIndex.SetRange( 0,  (short)m_nMsgLength-1 );
    m_omSpinStartBit.SetRange( 0, 7 );

    // Set Byte order (m_nDataFormat)
    CButton* pRadioIntel = (CButton*)GetDlgItem(IDC_RADIO_INTEL);
    CButton* pRadioMotorola = (CButton*)GetDlgItem(IDC_RADIO_MOTOROLA);
    if ((pRadioIntel != nullptr) && (pRadioMotorola != nullptr))
    {
        if ( m_nDataFormat == eEndianess::eIntel )
        {
            pRadioIntel->SetCheck(BST_CHECKED);
            pRadioMotorola->SetCheck(BST_UNCHECKED);
        }
        else
        {
            pRadioIntel->SetCheck(BST_UNCHECKED);
            pRadioMotorola->SetCheck(BST_CHECKED);
        }
    }

    if ( m_unMode == MD_EDIT )
    {
        if (m_omStrSgType == defBOOLEAN)
        {
            m_omComboSgType.SetCurSel( 0 );

            // Disable the signal length
            GetDlgItem(IDC_EDIT_MIN)->EnableWindow(FALSE);
            GetDlgItem(IDC_EDIT_MAX)->EnableWindow(FALSE);
            GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(FALSE);
            GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(FALSE);
        }
        else if (m_omStrSgType == defUNSIGNED_INT)
        {
            m_omComboSgType.SetCurSel( 2 );
            // Disable the signal length
            GetDlgItem(IDC_EDIT_MIN)->EnableWindow(TRUE);
            GetDlgItem(IDC_EDIT_MAX)->EnableWindow(TRUE);
            GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(TRUE);
            GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(TRUE);

        }
        else if (m_omStrSgType == defSIGNED_INT)
        {
            m_omComboSgType.SetCurSel( 1 );
            // Disable the signal length
            GetDlgItem(IDC_EDIT_MIN)->EnableWindow(TRUE);
            GetDlgItem(IDC_EDIT_MAX)->EnableWindow(TRUE);
            GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(TRUE);
            GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(TRUE);

        }
        else if (m_omStrSgType == "")
        {
            m_omComboSgType.SetCurSel( -1 );
        }
    }
    else
    {
        if (m_unMode == MD_ADD)
        {
            // Default be BOOLEAN
            m_omComboSgType.SetCurSel( 0 );
            // Calculate maxmin values
            vCalculateMaxMinValues(minVal, maxVal);

            // Set values
            if (m_omStrSgType == defSIGNED_INT)
            {
                m_odMaxValue.vSetValue( maxVal.n64Value);
                m_odMinValue.vSetValue( minVal.n64Value );
            }
            else
            {
                m_odMaxValue.vSetValue( maxVal.un64Value);
                m_odMinValue.vSetValue( minVal.un64Value);
            }

            // Disable the signal MIN AND MAX
            GetDlgItem(IDC_EDIT_MIN)->EnableWindow(FALSE);
            GetDlgItem(IDC_EDIT_MAX)->EnableWindow(FALSE);
            GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(FALSE);
            GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(FALSE);
        }
    }
    if(m_unMode == MD_READ_ONLY)
    {
        if (m_omStrSgType == CHAR_BOOL)
        {
            m_omComboSgType.SetCurSel( 0 );
        }
        else if (m_omStrSgType == CHAR_UINT)
        {
            m_omComboSgType.SetCurSel( 2 );
        }
        else if (m_omStrSgType == CHAR_INT)
        {
            m_omComboSgType.SetCurSel( 1 );
        }
        else
        {
            // Invalid type found
            ASSERT( FALSE );
            m_omComboSgType.SetCurSel( -1 );
        }

        // Disable all controls
        // Use control name if it is defined
        m_odScale.SetReadOnly();
        m_odOffset.SetReadOnly();
        m_odMinValue.SetReadOnly();
        m_odMaxValue.SetReadOnly();

        // Use control ID if control is not defined
        CEdit* pomEdit = nullptr;
        // Unit edit box
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_UNIT));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Unit Signal Name
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_SGNAME));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Unit Signal Length
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_SGLEN));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Unit Signal Byte Index
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_BYINDEX));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Unit Signal Start Bit
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_STBIT));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Unit Signal Length
        pomEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_SGLEN));
        if( pomEdit != nullptr )
        {
            pomEdit->SetReadOnly();
        }
        // Init the pointer to nullptr to avoid unknown access
        pomEdit = nullptr;
        // These controls doesn't support read only property
        // So Disable these
        CWnd* pomWnd = nullptr;
        // Signal Type Combobox
        pomWnd = GetDlgItem(IDC_COMB_SGTYPE);
        if( pomWnd != nullptr )
        {
            pomWnd->EnableWindow(FALSE);
        }
        // Signal Length Spin control
        pomWnd = GetDlgItem(IDC_SPIN_SGLENGTH);
        if( pomWnd != nullptr )
        {
            pomWnd->EnableWindow(FALSE);
        }
        // Signal Byte index Spin control
        pomWnd = GetDlgItem(IDC_SPIN_BYINDEX);
        if( pomWnd != nullptr )
        {
            pomWnd->EnableWindow(FALSE);
        }
        // Signal Bit index Spin control
        pomWnd = GetDlgItem(IDC_SPIN_BIT);
        if( pomWnd != nullptr )
        {
            pomWnd->EnableWindow(FALSE);
        }

        // Hide Ok Button
        pomWnd = GetDlgItem(IDOK);
        if( pomWnd != nullptr )
        {
            pomWnd->ShowWindow(SW_HIDE);
        }

        // Now rename cancel button to "Close" and center the button
        CButton* pomCancelButton;
        pomCancelButton = (CButton*)GetDlgItem(IDCANCEL);
        if( pomCancelButton != nullptr )
        {
            pomCancelButton->SetWindowText(_(defSTR_CLOSE_MENU_TEXT));
            // Get the rect before centering
            CRect omRect1, omRect2;
            pomCancelButton->GetWindowRect(&omRect1);
            ScreenToClient(&omRect1);
            // Center will place the button vertically center too
            // To avoide this call SetWindowPos with old top value
            pomCancelButton->CenterWindow();
            pomCancelButton->GetWindowRect(&omRect2);
            ScreenToClient(&omRect2);
            // Change the top position
            pomCancelButton->SetWindowPos(&wndTop,
                                          omRect2.left,
                                          omRect1.top,
                                          omRect1.Width(),
                                          omRect1.Height(),
                                          SWP_SHOWWINDOW);
        }
    }

    if (m_omStrSgType == defSIGNED_INT)// for int should take signed value
    {
        m_odMaxValue.vSetSigned( TRUE );
        m_odMinValue.vSetSigned( TRUE );
    }
    else // Should take only +ve value
    {
        m_odMaxValue.vSetSigned( FALSE );
        m_odMinValue.vSetSigned( FALSE );
    }

    // Set edit values
    if ( !m_omStrMaxVal.IsEmpty() && !m_omStrMinVal.IsEmpty())
    {
        CString StrDecVal = m_omStrMaxVal;

        m_odMaxValue.SetWindowText( StrDecVal );
        m_omStrMaxVal = StrDecVal;//store Max decimal value

        StrDecVal = m_omStrMinVal;
        m_odMinValue.SetWindowText( StrDecVal );
        m_omStrMinVal = StrDecVal;//store Min decimal value
    }

    // Set offset edit control properties
    m_odOffset.vSetBase(BASE_DECIMAL);
    m_odOffset.vAcceptFloatingNum( TRUE );
    m_odOffset.vSetSigned( TRUE );

    // Set scale edit control properties
    m_odScale.vSetBase(BASE_DECIMAL);
    m_odScale.vAcceptFloatingNum( TRUE );


    // Set edit values
    if ( !m_omStrOffset.IsEmpty() && !m_omStrScale.IsEmpty())
    {
        m_odOffset.SetWindowText( m_omStrOffset );
        m_odScale.SetWindowText( m_omStrScale );
    }

    m_omStrFirstSignalName = m_omStrSignalName;
    // Save Current type selection
    m_omComboSgType.GetWindowText(m_omStrPrevSignalType);
    //KSS
    // Save the intial values
    vSetInitialData();
    //KSS

    // Should be true only if the signal length is changed
    m_bLenChanged = FALSE;

    if (m_unMode == MD_READ_ONLY)
    {
        SendDlgItemMessage(IDC_EDIT_PHYS_MIN, EM_SETREADONLY, TRUE);
        SendDlgItemMessage(IDC_EDIT_PHYS_MAX, EM_SETREADONLY, TRUE);
    }
    m_bUpdatingPhysical = false;
    vUpdatePhysicalRange();
    vUpdateSignalBits();
    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditSgname                                 */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal name
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  22.02.2002                                            */
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Removed the dead code of database structure access    */
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditSgname()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY )
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            m_omStrPrevSignalName = m_omStrSignalName;

            UpdateData(TRUE);

            if ( !m_omStrSignalName.IsEmpty() )
            {
                // if name is changed, update the changes
                if ( m_omStrPrevSignalName != m_omStrSignalName )
                {
                    // Get appropriate msg structure ptr
                    m_bIsDataSaved = FALSE;
                }
            }
            else
            {
                /*AfxMessageBox( "Signal name cannot be empty",
                    MB_OK|MB_ICONINFORMATION);
                GetDlgItem(IDC_EDIT_SGNAME)->SetFocus();*/
            }
        }
    }
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditByindex                                */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal byte index
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  22.02.2002                                            */
/*  Modifications    :
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditByindex()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY)
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            SHORT shPrevByteIndex = m_shByteIndex+1;

            UpdateData(TRUE);

            if (shPrevByteIndex != m_shByteIndex+1)
            {
                m_bIsDataSaved = FALSE;
            }
        }
    }

}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditFactor                                 */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal factor
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  22.02.2002                                            */
/*  Modifications    :
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditFactor()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY)
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            m_bIsDataSaved = FALSE;
        }
    }
    vUpdatePhysicalRange();
}
/******************************************************************************/
/*  Function Name    :  bIsEditMinMaxValueValid                               */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :  BOOL                                                  */
/*  Functionality    :  Validates signal maximum and minimum value            */
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Krishnaswamy B.N                                      */
/*  Date Created     :  28.08.2003                                            */
/******************************************************************************/
BOOL CSignalDetailsDlg::bIsEditMinMaxValueValid()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true

    BOOL bRetVal = TRUE;

    if ( !m_bIsCanceled )
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            CString omstrFactorValue = "";

            m_odScale.GetWindowText(omstrFactorValue);

            // Get max and min values
            m_odMaxValue.GetWindowText( m_omStrMaxVal );
            m_odMinValue.GetWindowText( m_omStrMinVal );
            UINT unMaxLength = m_omStrMaxVal.GetLength();
            UINT unMinLength = m_omStrMinVal.GetLength();

            UINT unFactorValue = omstrFactorValue.GetLength();

            //check for the empty string
            if(unMinLength == 0)
            {
                AfxMessageBox( _("Minimum value field can't be empty!"),
                               MB_OK|MB_ICONINFORMATION);
                UpdateData(FALSE);
                GetDlgItem(IDC_EDIT_MIN)->SetFocus();
                bRetVal = FALSE;
            }
            else if(unMaxLength == 0)
            {
                AfxMessageBox( _("Maximum value field can't be empty!"),
                               MB_OK|MB_ICONINFORMATION);
                UpdateData(FALSE);
                GetDlgItem(IDC_EDIT_MAX)->SetFocus();
                bRetVal = FALSE;
            }//check for the boundary condition
            else if( bIsMaximumValueValid() == FALSE)
            {
                UpdateData(FALSE);
                GetDlgItem(IDC_EDIT_MAX)->SetFocus();
                bRetVal = FALSE;
            }
            else if(unFactorValue == 0)
            {
                AfxMessageBox( _("Factor value field can't be empty!"),
                               MB_OK|MB_ICONINFORMATION);
                UpdateData(FALSE);
                GetDlgItem(IDC_EDIT_FACTOR)->SetFocus();
                bRetVal = FALSE;
            }
            else if(m_odMaxValue.lGetValue() == m_odMinValue.lGetValue())
            {
                AfxMessageBox( _("Maximum and minimum values cannot be equal"),
                               MB_OK|MB_ICONINFORMATION);
                UpdateData(FALSE);
                GetDlgItem(IDC_EDIT_MIN)->SetFocus();
                bRetVal = FALSE;
            }
            else
            {
                if(bIsMinimumValueValid() == FALSE)
                {
                    UpdateData(FALSE);
                    GetDlgItem(IDC_EDIT_MIN)->SetFocus();
                    bRetVal = FALSE;
                }
            }
        }
    }
    return bRetVal;
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditOffset                                 */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal Offset value
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  22.02.2002                                            */
/*  Modifications    :
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditOffset()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY)
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            m_bIsDataSaved = FALSE;
        }
    }
    vUpdatePhysicalRange();
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditSgLen                                  */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal length
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  22.02.2002                                            */
/*  Modifications    :  rajesh: 05.03.2003 : Min-max values changes
/*. Modifications    :  Amitesh Bharti, 04.07.2003,                           */
/*                        Signal length going beyond message length is        */
/*                         validated                                          */
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Modified to get refer inactive database structure for */
/*                      editor operation                                      */
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditSglen()
{
    SaveSigLength();
    vUpdatePhysicalRange();
}

/******************************************************************************/
/*  Function Name    :  SaveSigLength()                                       */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  saves length and validates the range of data          */
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Ashwin. R. Uchil                                      */
/*  Date Created     :  1-8-2012                                              */
/******************************************************************************/
BOOL CSignalDetailsDlg::SaveSigLength()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true

    SIG_VALUE minVal, maxVal;
    BOOL bError = FALSE;
    if ( !m_bIsCanceled && (m_unMode != MD_READ_ONLY))
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            // Validate signal length
            // depending on the type
            CString omStrPrevSgName = "";

            m_omComboSgType.GetWindowText(omStrPrevSgName);
            if ( !omStrPrevSgName.IsEmpty() )
            {
                if ( !omStrPrevSgName.CompareNoCase(defBOOLEAN) )
                {
                    // If Boolean valid range is [1]
                    m_unMinVal = 1;
                }
                else if ( !omStrPrevSgName.CompareNoCase(defSIGNED_INT) )
                {
                    // If Signed INT valid range is 2-8
                    m_unMinVal = 2;
                }
                else if(!omStrPrevSgName.CompareNoCase(defUNSIGNED_INT) )
                {
                    // If unsigned INT valid range is 1-8
                    m_unMinVal = 1;
                }
            }

            BOOL bIsValid = UpdateData(TRUE);

            if(bIsValid == TRUE)
            {
                bError = FALSE;
            }
            else
            {
                bError = TRUE;
            }

            if ( !omStrPrevSgName.IsEmpty() )
            {
                if ( !omStrPrevSgName.CompareNoCase(defBOOLEAN) )
                {
                    // If Boolean valid range is [1]
                    if (m_unSgLen > 1)
                    {
                        m_unMinVal = 1;
                        m_unSgLen = 1;
                        bError = TRUE;
                        GetDlgItem(IDC_EDIT_SGLEN)->SetFocus();
                    }
                }
                else if ( !omStrPrevSgName.CompareNoCase(defSIGNED_INT) )
                {
                    // If Signed INT valid range is 2-8
                    if (m_unSgLen < 2)
                    {
                        m_unMinVal = 2;
                        bError = TRUE;
                        GetDlgItem(IDC_EDIT_SGLEN)->SetFocus();
                    }
                }
                else if(!omStrPrevSgName.CompareNoCase(defUNSIGNED_INT) )
                {
                    // If unsigned INT valid range is 1-8
                    if(m_unSgLen < 1)
                    {
                        m_unMinVal = 1;
                        bError = TRUE;
                        GetDlgItem(IDC_EDIT_SGLEN)->SetFocus();
                    }
                }
            }

            if ( bError != TRUE)
            {
                if ( m_unSgLen > 0 )
                {
                    // check for duplicate start bit value
                    // Auto-Update the max and min value
                    // depending on signal byte index, bit index and type
                    if ( m_bLenChanged == TRUE)
                    {
                        UINT unMaxSignalLength =
                            static_cast<UINT> (m_nMsgLength * 8);
                        if ( m_unSgLen <= unMaxSignalLength )
                        {
                            vCalculateMaxMinValues(minVal, maxVal);
                            if( !omStrPrevSgName.CompareNoCase(defSIGNED_INT))
                            {
                                m_odMaxValue.vSetValue( maxVal.n64Value);
                                m_odMinValue.vSetValue( minVal.n64Value);
                            }
                            else
                            {
                                m_odMaxValue.vSetValue( maxVal.un64Value);
                                m_odMinValue.vSetValue( minVal.un64Value);
                            }
                        }
                    }
                    else
                    {
                        // Set values
                        m_odMaxValue.vSetValue( m_odMaxValue.lGetValue() );
                        m_odMinValue.vSetValue( m_odMinValue.lGetValue() );
                    }

                    m_bDupliacateFound = FALSE;
                    m_bIsDataSaved = FALSE;
                }
                else
                {
                    m_unSgLen = 1;
                    bError = TRUE;
                }
            }
            if ( bError == TRUE )
            {
                UpdateData(FALSE);
            }
        }
    }
    return bError;
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditStBit                                  */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal start bit
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*  Modifications    :  Raja N on 10.03.2004
/*                      Removed the dead code of accessing the database       */
/*                      structure                                             */
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditStbit()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY)
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            BYTE unPrevSgStBit = m_byStartBit;

            UpdateData(TRUE);

            if (unPrevSgStBit != m_byStartBit)
            {
                m_bIsDataSaved = FALSE;
            }
        }
    }
}
/******************************************************************************/
/*  Function Name    :  OnKillfocusEditUnit                                   */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Validates signal unit
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*  Modifications    :
/*  Modification     :  Raja N on 10.03.2004                                  */
/*                      Added check for READ_ONLY mode to avoide manipulations*/
/******************************************************************************/
void CSignalDetailsDlg::OnKillfocusEditUnit()
{
    // check if the user has pressed cancel button
    // if yes, skip validation
    // if the user presses escape button,
    // "m_bIsCanceled" will be set to true
    if ( !m_bIsCanceled && m_unMode != MD_READ_ONLY)
    {
        CButton* pCancelButton1 =
            (CButton*)GetDlgItem(IDCANCEL)->GetFocus();

        CButton* pCancelButton2 =
            (CButton*)GetDlgItem(IDCANCEL);

        if ( pCancelButton1 != pCancelButton2 )
        {
            CString omStrPrevSgUnit = m_omStrUnit;

            UpdateData(TRUE);

            // if name is changed, update the changes
            if ( omStrPrevSgUnit != m_omStrUnit )
            {
                m_bIsDataSaved = FALSE;
            }
        }
    }

}
/******************************************************************************/
/*  Function Name    :  OnSelchangeCombSgtype                                 */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  On change of signal type, corresponding value
                        is set and max and min values are calculated
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*  Modifications    :  Rajesh Kumar : 04.03.2003
/*                      min max type changes
/*  Modifications    :  Raja N on 10.03.2004                                  */
/*                      Added code to check the previous selection before     */
/*                      changing the signal len to default                    */
/*  Modifications    :  Raja N on 22.07.2004                                  */
/*                      Removed code to set negative value for the Max/Min    */
/*                      values as these are displayed in hex format           */
/*  Modifications    :  Raja N on 31.07.2004, Added checks of returned pointer*/
/******************************************************************************/
void CSignalDetailsDlg::OnSelchangeCombSgtype()
{
    if ( !m_bIsCanceled )
    {
        SIG_VALUE minVal, maxVal;
        CWnd* pomWnd = nullptr;
        CButton* pCancelButton1 = nullptr;
        CButton* pCancelButton2 = nullptr;
        // Get
        pomWnd = GetDlgItem(IDCANCEL);
        if( pomWnd != nullptr )
        {
            pCancelButton1 = static_cast<CButton*>(pomWnd->GetFocus());
        }
        pCancelButton2 = static_cast<CButton*>(GetDlgItem(IDCANCEL));
        if ( pCancelButton1 != pCancelButton2 )
        {
            CString omStrPrevSgName = "";

            m_omComboSgType.GetWindowText(omStrPrevSgName);

            if ( !omStrPrevSgName.IsEmpty()
                    && m_omStrPrevSignalType != omStrPrevSgName )
            {
                //---------Added by ashwin for setting the signal length---------------------------
                CMsgSignal* pTempMsgSg = nullptr;
                UINT  unSigLength = 0;
                pTempMsgSg = m_sDbParams.m_pouMsgSignalActiveDB;
                sMESSAGE* psMSG = pTempMsgSg ->psGetMessagePointerInactive(m_omStrMsgName);
                sSIGNALS* psSignal = nullptr;
                if(psMSG != nullptr)
                {
                    psSignal = psMSG->m_psSignals;
                    while(psSignal)
                    {
                        if(m_omStrPrevSignalName != psSignal->m_omStrSignalName) //if its the same signal, don't add the length
                        {
                            //because the current signal length should be availabe for editing
                            unSigLength += psSignal->m_unSignalLength;
                        }
                        psSignal = psSignal->m_psNextSignalList;
                    }
                }
                //----------------------------------------------------------------------------------
                // Auto-update the signal length
                // for a given signal type
                if (!omStrPrevSgName.CompareNoCase(defBOOLEAN))
                {
                    m_unSgLen = 1;
                }
                else if ( !omStrPrevSgName.CompareNoCase(defUNSIGNED_INT)
                          ||  !omStrPrevSgName.CompareNoCase(defSIGNED_INT) )
                {
                    m_unSgLen = (m_nMsgLength * 8) - unSigLength; //assign the remaining bits as signal length
                    if(m_unSgLen > 64)
                    {
                        m_unSgLen = 64;
                    }
                }

                // Auto-Update the max and min value
                // depending on signal byte index, bit index and type
                vCalculateMaxMinValues(minVal, maxVal);
                // Set values

                // Should take only Decimal value
                if( !omStrPrevSgName.CompareNoCase(defSIGNED_INT))
                {
                    m_odMaxValue.vSetSigned( TRUE );
                    m_odMinValue.vSetSigned( TRUE );
                    m_odMaxValue.vSetValue( maxVal.n64Value);
                    m_odMinValue.vSetValue( minVal.n64Value);
                }
                else
                {
                    m_odMaxValue.vSetSigned( FALSE );
                    m_odMinValue.vSetSigned( FALSE );
                    m_odMaxValue.vSetValue( maxVal.un64Value);
                    m_odMinValue.vSetValue( minVal.un64Value);
                }
                // rajesh : 04.03.2003: END

                m_bIsDataSaved = FALSE;

                if ( !omStrPrevSgName.CompareNoCase(defBOOLEAN))
                {
                    GetDlgItem(IDC_EDIT_MIN)->EnableWindow(FALSE);
                    GetDlgItem(IDC_EDIT_MAX)->EnableWindow(FALSE);
                    GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(FALSE);
                    GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(FALSE);
                }
                else
                {
                    GetDlgItem(IDC_EDIT_MIN)->EnableWindow(TRUE);
                    GetDlgItem(IDC_EDIT_MAX)->EnableWindow(TRUE);
                    GetDlgItem(IDC_SPIN_SGLENGTH)->EnableWindow(TRUE);
                    GetDlgItem(IDC_EDIT_SGLEN)->EnableWindow(TRUE);
                }

                //Store Current Selection
                m_omStrPrevSignalType = omStrPrevSgName;

                UpdateData(FALSE);
                vUpdatePhysicalRange();
                vUpdateSignalBits();
            }
        }
    }
}

/******************************************************************************/
/*  Function Name    :  OnCancel                                              */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Sets a flag and calls base class function
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*  Modifications    :
/******************************************************************************/
void CSignalDetailsDlg::OnCancel()
{
    m_bIsCanceled = TRUE;

    CDialog::OnCancel();
}

/******************************************************************************/
/*  Function Name    :  OnOk                                                  */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Updates the signal details to the data structure      */
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*. Modifications    :  Amitesh Bharti, 05.06.2003,                           */
/*                                  replace datatype for supporting 64bits to */
/*                                   __int64                                  */
/*. Modifications    :  Amitesh Bharti, 04.07.2003,                           */
/*                        Signal length going beyond message length is        */
/*                         validated                                          */
/*. Modifications    :  Krishnaswamy B.N 28.08.2003,                          */
/*                      Checking for the validity of maximum and minimum value*/
/*  Modifications    :  Raja N.                                               */
/*                      14.01.2004, The memory allocation failure message was */
/*                      thrown in a wrong else condtion. Corrected            */
/*  Modifications    :  Raja N on 10.03.2004                                  */
/*                      Modified to get refer inactive database structure for */
/*                      editor operation                                      */
/*  Modifications    :  Raja N on 22.07.2004                                  */
/*                      Added check to prevent processing data in read only   */
/*                      mode and modified the value to extend sign bit        */
/*  Modifications    :  Raja N on 31.07.2004, Removed hardcoded strings       */
/******************************************************************************/
void CSignalDetailsDlg::OnOK()
{
    BOOL bReturnFlag = TRUE;
    BOOL bModifiedFlag = FALSE;
    // BUSMASTER-NG: Enter in a physical value does not move the focus, take the value here
    CWnd* pomFocus = GetFocus();
    if ((pomFocus != nullptr) && (pomFocus->GetDlgCtrlID() == IDC_EDIT_PHYS_MIN))
    {
        vSetRawFromPhysical(m_odMinValue, IDC_EDIT_PHYS_MIN);
    }
    else if ((pomFocus != nullptr) && (pomFocus->GetDlgCtrlID() == IDC_EDIT_PHYS_MAX))
    {
        vSetRawFromPhysical(m_odMaxValue, IDC_EDIT_PHYS_MAX);
    }
    BOOL bIsInValid = SaveSigLength();

    if (bIsInValid == TRUE)    //If any data is invalid, just return
    {
        return;
    }

    // UPdate the structure only if something is modified
    if(m_unMode != MD_READ_ONLY)
    {
        BOOL bValidData = UpdateData(TRUE);
        if (!bValidData)    //If any data is invalid, just return
        {
            return;
        }
        if (bIsDataModified())
        {
            bModifiedFlag = TRUE;
            if ( m_omStrSignalName.IsEmpty() )
            {
                AfxMessageBox( _(defSTR_SIGNAL_NAME_INVALID),
                               MB_OK | MB_ICONINFORMATION );
                GetDlgItem(IDC_EDIT_SGNAME)->SetFocus();
                bReturnFlag = FALSE;
            }
            else if(bIsEditMinMaxValueValid() == FALSE)
            {
                bReturnFlag = FALSE;
            }
            else if(bReturnFlag == TRUE)
            {
                // Get appropriate msg structure ptr
                CMsgSignal* pTempMsgSg = m_sDbParams.m_pouMsgSignalActiveDB;

                if(pTempMsgSg != nullptr)
                {
                    if ( m_bNameChanged &&
                            (pTempMsgSg->bIsDuplicateSignalName( m_omStrMsgName,
                                    m_omStrSignalName)) && bReturnFlag == TRUE)
                    {
                        AfxMessageBox( _(MSG_DUPLICATE_SG_NAME), MB_OK|MB_ICONINFORMATION);
                        m_omStrSignalName.Empty();
                        UpdateData(FALSE);
                        GetDlgItem( IDC_EDIT_SGNAME )->SetFocus();
                        m_bNameChanged = FALSE;
                        bReturnFlag = FALSE;
                    }
                    else if(m_bNameChanged == TRUE && bReturnFlag == TRUE)
                    {
                        // Check if the signal name configured is valid
                        BOOL bIsSignalNameValid = ValidateSignalShortName(m_omStrSignalName);

                        if(bIsSignalNameValid == FALSE)
                        {
                            AfxMessageBox( _(MSG_INVALID_SG_NAME), MB_OK|MB_ICONINFORMATION);
                            m_omStrSignalName.Empty();
                            UpdateData(FALSE);
                            GetDlgItem( IDC_EDIT_SGNAME )->SetFocus();
                            m_bNameChanged = FALSE;
                            bReturnFlag = FALSE;
                        }
                    }
                }
                if(!(CMsgSignal::bValidateSignal(m_nMsgLength, m_shByteIndex+1,
                                                 m_byStartBit, m_unSgLen,
                                                 ( eEndianess) m_nDataFormat ) ) &&
                        (bReturnFlag == TRUE))
                {
                    CString omStrMessage = _(defSTR_SIGNAL_END_BIT_INVALID);
                    if (m_nDataFormat == eEndianess::eMotorola)
                    {
                        // BUSMASTER-NG: the Motorola convention of the database surprises users (original issue 1315)
                        omStrMessage += _("\n\nFor a Motorola signal, Byte Index and Start Bit give the position of "
                                          "its least significant bit; the signal continues towards byte 0. "
                                          "Example: a 16 bit signal in bytes 0 and 1 has Byte Index 1 and Start Bit 0.");
                    }
                    AfxMessageBox( omStrMessage, MB_OK | MB_ICONINFORMATION );
                    GetDlgItem( IDC_EDIT_SGLEN )->SetFocus();
                    bReturnFlag = FALSE;
                }
                // check for duplicate start bit value
                if (nullptr != pTempMsgSg && pTempMsgSg->bIsDuplicateSignalStartBitValue( m_omStrMsgName,
                        m_shByteIndex+1, m_unSgLen, m_byStartBit, m_nDataFormat )
                        && bReturnFlag == TRUE)
                {
                    AfxMessageBox( _(defSTR_SIGNAL_DUP_START_BIT),
                                   MB_OK | MB_ICONINFORMATION );
                    GetDlgItem( IDC_EDIT_STBIT )->SetFocus();
                    bReturnFlag = FALSE;
                }

                if ( m_unMode == MD_ADD && bReturnFlag == TRUE && pTempMsgSg != nullptr )
                {
                    // Allocate memory to the new signal
                    pTempMsgSg->bAddSignalToMsg( m_omStrMsgName );
                }
                if ( bReturnFlag == TRUE)
                {
                    sSIGNALS* pSg = new sSIGNALS;

                    if ( pSg != nullptr && pTempMsgSg != nullptr )
                    {
                        pSg->m_eFormat = ( eEndianess) m_nDataFormat;
                        CString omStrSgType = "";
                        m_omComboSgType.GetLBText(
                            m_omComboSgType.GetCurSel(), omStrSgType );
                        if ( omStrSgType == defBOOLEAN )
                        {
                            pSg->m_bySignalType = CHAR_BOOL;
                        }
                        else if ( omStrSgType == defUNSIGNED_INT )
                        {
                            pSg->m_bySignalType = CHAR_UINT;
                        }
                        else if ( omStrSgType == defSIGNED_INT )
                        {
                            pSg->m_bySignalType = CHAR_INT;
                        }

                        CString strTemp;
                        GetDlgItem(IDC_EDIT_FACTOR)->GetWindowText(strTemp);
                        // BUSMASTER-NG: '.' or ',' as decimal symbol, full precision
                        pSg->m_fSignalFactor = DbfNumber::dParse(strTemp);

                        if(pSg->m_bySignalType == CHAR_INT)
                        {
                            pSg->m_SignalMaxValue.n64Value   =
                                (__int64)m_odMaxValue.lGetValue();
                            pSg->m_SignalMinValue.n64Value   =
                                (__int64)m_odMinValue.lGetValue();
                            // Change the values to get app. decimal value
                            if( m_unSgLen < defMAX_BITS )
                            {
                                // Extend the sign bit to actual value
                                CUtilFunctions::s_vExtendSignBit(
                                    pSg->m_SignalMaxValue.n64Value,
                                    m_unSgLen );
                                CUtilFunctions::s_vExtendSignBit(
                                    pSg->m_SignalMinValue.n64Value,
                                    m_unSgLen );
                            }
                        }
                        else
                        {
                            pSg->m_SignalMaxValue.un64Value   =
                                (unsigned __int64)m_odMaxValue.lGetValue();
                            pSg->m_SignalMinValue.un64Value   =
                                (unsigned __int64)m_odMinValue.lGetValue();
                        }

                        GetDlgItem(IDC_EDIT_OFFSET)->GetWindowText(strTemp);
                        pSg->m_fSignalOffset            = DbfNumber::dParse(strTemp);
                        pSg->m_omStrSignalName          = m_omStrSignalName;
                        pSg->m_omStrSignalUnit          = m_omStrUnit;
                        pSg->m_unSignalLength           = m_unSgLen;
                        pSg->m_unStartByte              = m_shByteIndex+1;
                        pSg->m_byStartBit               = m_byStartBit;

                        if ( m_unMode == MD_ADD )
                        {
                            m_omStrPrevSignalName = "";
                        }

                        // Fill the matrix for edited signal
                        pTempMsgSg->vUpdateSignalDetails( m_omStrMsgName,
                                                          m_omStrPrevSignalName,  pSg );

                        // Now saved
                        m_bIsDataSaved = TRUE;
                        delete pSg;
                        pSg = nullptr;
                    }
                    else
                    {
                        AfxMessageBox(_(MSG_MEMORY_CONSTRAINT), MB_OK|MB_ICONINFORMATION);
                    }
                }
            }
        }
    }

    if(bReturnFlag == TRUE)//no error occurs
    {
        if(bModifiedFlag == TRUE)//some modification done
        {
            CDialog::OnOK();
        }
        else //no modification
        {
            CDialog::OnCancel();
        }
    }
}

BOOL CSignalDetailsDlg::ValidateSignalShortName(CString omStrSignalShortName)
{
    BOOL bValid = FALSE;
    TCHAR buffer[256];

    int nChar;
    int nlen;

    _tcscpy(buffer,omStrSignalShortName);

    nlen=(int)_tcslen(buffer);

    if(nlen > 0)
    {
        nChar = buffer[0];

        // Check if the first character in the name is a digit
        if((nChar >= '0' && nChar<= '9'))
        {
            bValid = FALSE;
        }
        else
        {
            bValid = TRUE;
        }

        if(bValid == TRUE)
        {
            // Check if the first character in the name is not in between [a-z][A-Z] and _
            if(!((nChar >= 'A' && nChar <= 'Z') ||
                    (nChar >= 'a' && nChar<= 'z') ||
                    (nChar >= '0' && nChar<= '9') ||
                    (nChar == '_')))
            {
                bValid = FALSE;
            }
        }
    }

    if(bValid == TRUE)
    {
        for(int ni=1; ni<nlen; ni++)
        {
            nChar = buffer[ni];

            if(((nChar >= 'A' && nChar <= 'Z') ||
                    (nChar >= 'a' && nChar<= 'z') ||
                    (nChar >= '0' && nChar<= '9') ||
                    (nChar == '_')))
            {
                bValid = TRUE;
            }
            else
            {
                bValid = FALSE;
                break;
            }
        }
    }
    return bValid;
}

/******************************************************************************/
/*  Function Name    :  vCalculateMaxMinValues                                */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Calculates max and mininmum value
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.02.2002                                            */
/*  Modifications    :  Rajesh Kumar 04.03.2003 - arguments changed           */
/*  Modifications    :  Amitesh Bharti, 05.06.2003,                           */
/*                      replace datatype for supporting 64bits to __int64     */
/*  Modifications    :  Raja N, 22.07.2004                                    */
/*                      Modified the Min and Max calculation for -ve value fix*/
/*  Modifications    :  Raja N on 31.07.2004, Removed hardcoded strings       */
/******************************************************************************/
void CSignalDetailsDlg::vCalculateMaxMinValues(SIG_VALUE& rMinVal,
        SIG_VALUE& rMaxVal)
{
    // Get signal type
    CString omStrSgType = "";

    m_omComboSgType.GetWindowText(omStrSgType );

    // if signal type is bool and signal length is 1
    // then set max and min values to 1 and 0
    if ( !omStrSgType.CompareNoCase(defBOOLEAN) && m_unSgLen == 1 )
    {
        rMinVal.un64Value = FALSE;
        rMaxVal.un64Value = TRUE;
    }
    // if signal type is UINT the max value is {(2**sigLen)-1}
    else if ( !omStrSgType.CompareNoCase(defUNSIGNED_INT) )
    {
        rMinVal.un64Value = 0;
        rMaxVal.un64Value = 1;
        UINT nPower = m_unSgLen;
        //        if(m_unSgLen == defMAX_BITS )
        //        {
        //            nPower = m_unSgLen - 1;
        //        }
        // Get the 2 power Signal Len
        // 2 ^ n == 2 << n
        rMaxVal.un64Value <<= nPower;
        // Subtract one to get max val
        rMaxVal.un64Value = rMaxVal.un64Value - 1;
    }
    // if signal type is INT the max and min values are
    // {(2**(sigLen-1))-1} and -{(2**(sigLen-1))-1}
    else if ( !omStrSgType.CompareNoCase(defSIGNED_INT) )
    {
        rMaxVal.n64Value = 1;
        // The Range is from - (2 ^ Signal Len) to + ( min - 1 )
        // say for 8 bits it is - (128) to + (127)
        // Find the min first
        rMinVal.n64Value = rMaxVal.n64Value << (m_unSgLen - 1);
        rMaxVal.n64Value = rMinVal.n64Value - 1;
        rMinVal.n64Value = -(rMinVal.n64Value);
    }

    m_bLenChanged = FALSE;
    UpdateData( FALSE );
}

/******************************************************************************/
/*  Function Name    :  OnChangeEditSglen                                     */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Sets a flag to oindicate a change
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  25.04.2002                                            */
/*  Modifications    :
/******************************************************************************/
void CSignalDetailsDlg::OnChangeEditSglen()
{
    // TODO: If this is a RICHEDIT control, the control will not
    // send this notification unless you override the CDialog::OnInitDialog()
    // function and call CRichEditCtrl().SetEventMask()
    // with the ENM_CHANGE flag ORed into the mask.

    m_bLenChanged = TRUE;
    vUpdateSignalBits();
}
/******************************************************************************/
/*  Function Name    :  OnChangeEditSgname                                    */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :                                                        */
/*  Functionality    :  Sets a flag to oindicate a change
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  20.05.2002                                            */
/*  Modifications    :
/******************************************************************************/

void CSignalDetailsDlg::OnChangeEditSgname()
{
    // TODO: If this is a RICHEDIT control, the control will not
    // send this notification unless you override the CDialog::OnInitDialog()
    // function and call CRichEditCtrl().SetEventMask()
    // with the ENM_CHANGE flag ORed into the mask.

    // TODO: Add your control notification handler code here
    m_bNameChanged = TRUE;
}
/******************************************************************************/
/*  Function Name    :  PreTranslateMessage                                   */
/*                                                                            */
/*  Input(s)         :  MSG* pMsg                                             */
/*  Output           :  BOOL                                                  */
/*  Functionality    :  Doesn't process a space
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Amarnath Shastry                                      */
/*  Date Created     :  23.10.2002                                            */
/*  Modifications    :  Raja N on 31.07.2004, Removed hardcoded strings       */
/******************************************************************************/
BOOL CSignalDetailsDlg::PreTranslateMessage(MSG* pMsg)
{
    // Capture the space character and
    // do not process the same
    BOOL bSkip = FALSE;
    if ( pMsg->message == WM_CHAR )
    {
        if ( pMsg->wParam == defEMPTY_CHAR)
        {
            bSkip = TRUE;
        }
    }
    if ( bSkip == FALSE)
    {
        bSkip = CDialog::PreTranslateMessage(pMsg);
    }

    return bSkip;
}
/******************************************************************************/
/*  Function Name    :  bIsMaximumValueValid                                  */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :  BOOL                                                  */
/*  Functionality    :  Checks for the validity of maximum value              */
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Krishnaswamy B.N                                      */
/*  Date Created     :  28.08.2003                                            */
/*  Modifications    :  Raja N on 22.07.2004, Added function call to extend   */
/*                      the sign bit to get actual value for signed number    */
/*  Modifications    :  Raja N on 31.07.2004, Removed hardcoded strings       */
/******************************************************************************/
BOOL CSignalDetailsDlg::bIsMaximumValueValid()
{
    BOOL bRetVal = TRUE;
    SIG_VALUE  minVal,maxVal;
    CString strSignalType;
    m_omComboSgType.GetLBText(m_omComboSgType.GetCurSel(), strSignalType);
    CMainFrame* pMainFrame = static_cast<CMainFrame*> (AfxGetApp()->m_pMainWnd);

    if( !strSignalType.CompareNoCase(defSIGNED_INT) )
    {

        __int64 n64MaxVal, n64MinVal;
        if(theApp.pouGetFlagsPtr()->nGetFlagStatus(HEX))
        {
            n64MaxVal = _strtoi64(m_omStrMaxVal, nullptr, 16);
            n64MinVal = _strtoi64(m_omStrMinVal, nullptr, 16);

            _int64 n64TempValue = 1;
            n64TempValue = n64TempValue << (m_unSgLen -1) ;
            if(n64MinVal >= n64TempValue)
            {
                n64MinVal = n64MinVal - (2 * n64TempValue);
            }

        }
        else
        {
            n64MaxVal = _strtoi64(m_omStrMaxVal, nullptr, 10);
            n64MinVal = _strtoi64(m_omStrMinVal, nullptr, 10);
        }
        // Extend the Sign bit
        //        CUtilFunctions::s_vExtendSignBit( n64MinVal, m_unSgLen );
        //      CUtilFunctions::s_vExtendSignBit( n64, m_unSgLen );

        if ( n64MaxVal < n64MinVal )
        {
            AfxMessageBox(_(MSG_MAX_VAL_LESS), MB_OK|MB_ICONINFORMATION);
            bRetVal = FALSE;
        }
        else
        {
            if ( m_unSgLen <= defMAX_BITS )
            {
                // Get possible max and min value
                vCalculateMaxMinValues( minVal, maxVal);
                if ( n64MaxVal > maxVal.n64Value )
                {
                    AfxMessageBox( _(defSTR_MAX_VAL_INVALID),
                                   MB_OK | MB_ICONINFORMATION );
                    m_odMaxValue.vSetValue( maxVal.n64Value );
                    bRetVal = FALSE;
                }
                else
                {
                    // Accept the changes
                    m_bIsDataSaved = FALSE;
                }
            }
            else
            {
                // Accept the changes
                m_bIsDataSaved = FALSE;
            }
        }
    }
    else // unsigned int / boolean
    {
        unsigned __int64 un64MaxVal, un64MinVal;
        if(theApp.pouGetFlagsPtr()->nGetFlagStatus(HEX))
        {
            // BUSMASTER-NG: no sign extension here, the value is unsigned (min B4 and max EA were refused)
            un64MaxVal = _strtoui64(m_omStrMaxVal, nullptr, 16);
            un64MinVal = _strtoui64(m_omStrMinVal, nullptr, 16);
        }
        else
        {
            un64MaxVal = _strtoui64(m_omStrMaxVal, nullptr, 10);
            un64MinVal = _strtoui64(m_omStrMinVal, nullptr, 10);

        }
        if ( un64MaxVal < un64MinVal && bRetVal == TRUE)
        {
            AfxMessageBox(_(MSG_MAX_VAL_LESS), MB_OK|MB_ICONINFORMATION);
            bRetVal = FALSE;
        }
        else if(bRetVal == TRUE)
        {
            if ( m_unSgLen <= defMAX_BITS )
            {
                // Get possible max and min value
                vCalculateMaxMinValues( minVal, maxVal);
                if ( un64MaxVal > maxVal.un64Value )
                {
                    AfxMessageBox( _(defSTR_MAX_VAL_INVALID),
                                   MB_OK | MB_ICONINFORMATION );
                    m_odMaxValue.vSetValue(maxVal.un64Value);
                    bRetVal = FALSE;
                }
                else
                {
                    // Accept the changes
                    m_bIsDataSaved = FALSE;
                }
            }
            else
            {
                // Accept the changes
                m_bIsDataSaved = FALSE;
            }
        }
    }
    return bRetVal;
}
/******************************************************************************/
/*  Function Name    :  bIsMinimumValueValid                                  */
/*                                                                            */
/*  Input(s)         :                                                        */
/*  Output           :  BOOL                                                  */
/*  Functionality    :  Checks for the validity of minimum value              */
/*  Member of        :  CSignalDetailsDlg                                     */
/*  Friend of        :      -                                                 */
/*                                                                            */
/*  Author(s)        :  Krishnaswamy B.N                                      */
/*  Date Created     :  28.08.2003                                            */
/*  Modifications    :  Raja N on 22.07.2004, Added function call to extend   */
/*                      the sign bit to get actual value for signed number    */
/*  Modifications    :  Raja N on 31.07.2004, Removed hardcoded strings       */
/******************************************************************************/
BOOL CSignalDetailsDlg::bIsMinimumValueValid()
{
    BOOL bRetVal = TRUE;
    SIG_VALUE  minVal,maxVal;
    CString strSignalType;
    m_omComboSgType.GetLBText(m_omComboSgType.GetCurSel(), strSignalType);
    CMainFrame* pMainFrame = static_cast<CMainFrame*> (AfxGetApp()->m_pMainWnd);

    if( !strSignalType.CompareNoCase(defSIGNED_INT))
    {
        //__int64 n64 = 0;
        //if(pMainFrame != nullptr)
        //{
        //n64 = pMainFrame->nConvertStringToInt(m_omStrMinVal);
        //    // Extend the Sign Bit
        //    CUtilFunctions::s_vExtendSignBit( n64, m_unSgLen );
        //}


        //        CUtilFunctions::s_vExtendSignBit( n64MaxVal, m_unSgLen );
        __int64 n64MaxVal, n64MinVal;
        if(theApp.pouGetFlagsPtr()->nGetFlagStatus(HEX))
        {
            n64MaxVal = _strtoi64(m_omStrMaxVal, nullptr, 16);
            n64MinVal = _strtoi64(m_omStrMinVal, nullptr, 16);

            _int64 n64TempValue = 1;
            n64TempValue = n64TempValue << (m_unSgLen -1) ;
            if(n64MinVal >= n64TempValue)
            {
                n64MinVal = n64MinVal - (2 * n64TempValue);
            }

        }
        else
        {
            n64MaxVal = _strtoi64(m_omStrMaxVal, nullptr, 10);
            n64MinVal = _strtoi64(m_omStrMinVal, nullptr, 10);
        }

        if ( n64MinVal > n64MaxVal )
        {
            AfxMessageBox(MSG_MIN_VAL_MORE, MB_OK|MB_ICONINFORMATION);
            bRetVal = FALSE;
        }
        else
        {
            if ( m_unSgLen <= defMAX_BITS )
            {
                // Get possible max and min value
                vCalculateMaxMinValues( minVal,maxVal);
                // Extend the Sign Bit to get the actual value
                //                CUtilFunctions::s_vExtendSignBit( minVal.n64Value, m_unSgLen);
                if ( n64MinVal < minVal.n64Value)
                {
                    AfxMessageBox( _(defSTR_MIN_VAL_INVALID),
                                   MB_OK | MB_ICONINFORMATION );
                    m_odMinValue.vSetValue(minVal.n64Value);
                    bRetVal = FALSE;
                }
                else
                {
                    // Accept the changes
                    m_bIsDataSaved = FALSE;
                }
            }
            else
            {
                UpdateData(TRUE);
            }
        }
    }
    else // unsigned int
    {
        __int64 un64MinVal = 0;
        if(pMainFrame != nullptr)
        {
            un64MinVal = /*pMainFrame->nConvertStringToInt(m_omStrMinVal)*/m_odMinValue.lGetValue();
        }
        unsigned __int64 un64MaxVal   = m_odMaxValue.lGetUnsignedValue();
        if ( un64MinVal > un64MaxVal )
        {
            AfxMessageBox(_(MSG_MIN_VAL_MORE), MB_OK|MB_ICONINFORMATION);
            bRetVal = FALSE;
        }
        else
        {

            if ( m_unSgLen <= defMAX_BITS )
            {
                // Get possible max and min value
                vCalculateMaxMinValues( minVal,maxVal);
                if ( un64MinVal < minVal.un64Value)
                {
                    AfxMessageBox( _(defSTR_MIN_VAL_INVALID),
                                   MB_OK | MB_ICONINFORMATION );
                    m_odMinValue.vSetValue(minVal.un64Value);
                    bRetVal = FALSE;
                }
                else
                {
                    // Accept the changes
                    m_bIsDataSaved = FALSE;
                }
            }
            else
            {
                UpdateData(TRUE);
            }
        }
    }
    return bRetVal;
}

void CSignalDetailsDlg::OnBnClickedRadioIntel()
{
    m_nDataFormat = eEndianess::eIntel;
    vUpdateSignalBits();
}

void CSignalDetailsDlg::OnBnClickedRadioMotorola()
{
    m_nDataFormat = eEndianess::eMotorola;
    vUpdateSignalBits();
}
BOOL CSignalDetailsDlg::bIsDataModified()
{
    BOOL bChanged = FALSE;
    // Get the signal type
    m_omComboSgType.GetWindowText(m_omStrSgType);
    // Get the Max value
    m_odMaxValue.GetWindowText(m_omStrMaxVal);
    // Get the Min value
    m_odMinValue.GetWindowText(m_omStrMinVal);
    // Get the offset value
    m_odOffset.GetWindowText(m_omStrOffset);
    // Get the scale value
    m_odScale.GetWindowText(m_omStrScale);

    if ( m_nDataFormatInitial != m_nDataFormat ||
            m_unSgLenInitial != m_unSgLen ||
            m_omStrSignalNameInitial != m_omStrSignalName ||
            m_omStrUnitInitial != m_omStrUnit ||
            m_omStrMaxValInitial != m_omStrMaxVal ||
            m_omStrMinValInitial != m_omStrMinVal ||
            m_omStrOffsetInitial != m_omStrOffset ||
            m_omStrScaleInitial != m_omStrScale ||
            m_omStrSgTypeInitial != m_omStrSgType ||
            m_shByteIndexInitial != m_shByteIndex ||
            m_byStartBitInitial != m_byStartBit )
    {
        bChanged = TRUE;
    }
    return bChanged;
}
void CSignalDetailsDlg::vSetInitialData()
{
    m_nDataFormatInitial = m_nDataFormat;
    m_unSgLenInitial = m_unSgLen;
    m_omStrSignalNameInitial = m_omStrSignalName;
    m_omStrUnitInitial = m_omStrUnit;
    m_omStrMaxValInitial = m_omStrMaxVal;
    m_omStrMinValInitial = m_omStrMinVal;
    m_omStrOffsetInitial = m_omStrOffset;
    m_omStrScaleInitial = m_omStrScale;
    m_omStrSgTypeInitial = m_omStrSgType;
    m_shByteIndexInitial = m_shByteIndex;
    m_byStartBitInitial  = m_byStartBit;
}
bool CSignalDetailsDlg::IsRangeChanged()
{
    if((m_omStrMinVal != m_omStrMinValInitial)||
            (m_omStrMaxVal != m_omStrMaxValInitial))
    {
        return true;
    }
    else
    {
        return false;
    }
}
/******************************************************************************/
/*  BUSMASTER-NG: physical range and position of the signal in the message    */
/*                                                                            */
/*  The database stores the raw minimum and maximum as integers (this is what */
/*  DBManager.dll reads). The physical range raw * factor + offset is shown   */
/*  next to them and can be typed with decimals: the raw value is then        */
/*  rounded to the nearest step of the factor (original issues 1296 and 215). */
/******************************************************************************/
bool CSignalDetailsDlg::bGetFactorOffset(double& dFactor, double& dOffset)
{
    CString omStrText;
    m_odScale.GetWindowText(omStrText);
    dFactor = DbfNumber::dParse(omStrText);
    m_odOffset.GetWindowText(omStrText);
    dOffset = DbfNumber::dParse(omStrText);
    return (dFactor == dFactor) && (dFactor != 0.0) && (dOffset == dOffset);
}

static CString s_omFormatPhysical(double dValue)
{
    // 15 significant digits: 0.1 * 3 is shown as 0.3, not 0.30000000000000004
    char acText[64];
    _sprintf_s_l(acText, sizeof(acText), "%.15g", DbfNumber::hCLocale(), dValue);
    return DbfNumber::strFormat(DbfNumber::dParse(acText)).c_str();
}

void CSignalDetailsDlg::vUpdatePhysicalRange()
{
    if (m_bUpdatingPhysical || (GetDlgItem(IDC_EDIT_PHYS_MIN) == nullptr))
    {
        return;
    }
    m_bUpdatingPhysical = true;
    double dFactor, dOffset;
    CString omStrMin, omStrMax;
    if (bGetFactorOffset(dFactor, dOffset))
    {
        double dRawMin, dRawMax;
        if (m_odMinValue.bIsSigned())
        {
            dRawMin = (double)m_odMinValue.lGetValue();
            dRawMax = (double)m_odMaxValue.lGetValue();
        }
        else
        {
            dRawMin = (double)m_odMinValue.lGetUnsignedValue();
            dRawMax = (double)m_odMaxValue.lGetUnsignedValue();
        }
        omStrMin = s_omFormatPhysical(dRawMin * dFactor + dOffset);
        omStrMax = s_omFormatPhysical(dRawMax * dFactor + dOffset);
        if (dFactor < 0.0)
        {
            CString omStrTemp = omStrMin;
            omStrMin = omStrMax;
            omStrMax = omStrTemp;
        }
    }
    SetDlgItemText(IDC_EDIT_PHYS_MIN, omStrMin);
    SetDlgItemText(IDC_EDIT_PHYS_MAX, omStrMax);
    // Editable when the raw values are
    GetDlgItem(IDC_EDIT_PHYS_MIN)->EnableWindow(m_odMinValue.IsWindowEnabled());
    GetDlgItem(IDC_EDIT_PHYS_MAX)->EnableWindow(m_odMaxValue.IsWindowEnabled());
    m_bUpdatingPhysical = false;
}

void CSignalDetailsDlg::vSetRawFromPhysical(CRadixEdit& odRaw, UINT unPhysCtrlID)
{
    if (m_bUpdatingPhysical || (m_unMode == MD_READ_ONLY))
    {
        return;
    }
    CString omStrPhys;
    GetDlgItemText(unPhysCtrlID, omStrPhys);
    omStrPhys.Trim();
    double dFactor, dOffset;
    if (!omStrPhys.IsEmpty() && bGetFactorOffset(dFactor, dOffset))
    {
        double dRaw = floor((DbfNumber::dParse(omStrPhys) - dOffset) / dFactor + 0.5);
        if (!odRaw.bIsSigned() && (dRaw < 0.0))
        {
            dRaw = 0.0;
        }
        if ((dRaw > -9.2e18) && (dRaw < 9.2e18))
        {
            odRaw.vSetValue((__int64)dRaw);     // the physical value then shows the nearest possible one
            m_bIsDataSaved = FALSE;
        }
    }
    vUpdatePhysicalRange();
}

void CSignalDetailsDlg::OnChangeRawOrScaling()
{
    vUpdatePhysicalRange();
}

void CSignalDetailsDlg::OnKillfocusEditPhysMin()
{
    vSetRawFromPhysical(m_odMinValue, IDC_EDIT_PHYS_MIN);
}

void CSignalDetailsDlg::OnKillfocusEditPhysMax()
{
    vSetRawFromPhysical(m_odMaxValue, IDC_EDIT_PHYS_MAX);
}

void CSignalDetailsDlg::OnChangeBitPosition()
{
    vUpdateSignalBits();
}

void CSignalDetailsDlg::vUpdateSignalBits()
{
    CWnd* pomInfo = GetDlgItem(IDC_STAT_SIGNAL_BITS);
    if ((pomInfo == nullptr) || (GetDlgItem(IDC_EDIT_SGLEN) == nullptr))
    {
        return;
    }
    BOOL bOk1, bOk2, bOk3;
    int nByte = (int)GetDlgItemInt(IDC_EDIT_BYINDEX, &bOk1, FALSE);
    int nBit = (int)GetDlgItemInt(IDC_EDIT_STBIT, &bOk2, FALSE);
    int nLength = (int)GetDlgItemInt(IDC_EDIT_SGLEN, &bOk3, FALSE);
    CString omStrInfo;
    if (bOk1 && bOk2 && bOk3 && (nBit <= 7) && (nLength >= 1))
    {
        // The start position is the LSB; Intel signals grow towards the last byte, Motorola signals towards byte 0
        int nMsbBits = nBit + nLength - 1;
        int nMsbByte = (m_nDataFormat == eEndianess::eIntel) ? nByte + nMsbBits / 8 : nByte - nMsbBits / 8;
        int nMsbBit = nMsbBits % 8;
        // Start bit of the DBC format: LSB for Intel, MSB for Motorola
        int nDbcStartBit = (m_nDataFormat == eEndianess::eIntel) ? nByte * 8 + nBit : nMsbByte * 8 + nMsbBit;
        omStrInfo.Format(_("LSB: byte %d bit %d,  MSB: byte %d bit %d  (start bit %d in a DBC file)"),
                         nByte, nBit, nMsbByte, nMsbBit, nDbcStartBit);
        if ((nMsbByte < 0) || (nMsbByte >= (int)m_nMsgLength))
        {
            omStrInfo = CString(_("Outside the message: ")) + omStrInfo;
        }
    }
    pomInfo->SetWindowText(omStrInfo);
}
