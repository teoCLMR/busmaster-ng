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
 * \file      CAN_PEAK_PCANBasic.h
 * \brief     BUSMASTER-NG driver for PEAK-System CAN interfaces, through the PCAN-Basic API
 */
#pragma once

#ifndef __AFXWIN_H__
#error include 'CAN_PEAK_PCANBasic_stdafx.h' before including this file
#endif

#include "DIL_Interface/HardwareListingCAN.h"
#include "PCANBasicResource.h"

/**
 * The DLL object
 */
class CCAN_PEAK_PCANBasic : public CWinApp
{
public:
    CCAN_PEAK_PCANBasic();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};

/**
 * Handles the "Advanced" button and the baud rate check of the hardware selection dialog
 */
class CPCANBasicSettings : public IChangeRegisters
{
public:
    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelected);
    double vValidateBaudRate(double dBaudRate, int nChannel, UINT nClock);
};

/**
 * "Advanced" dialog: listen-only mode of one channel
 */
class CPCANBasicAdvancedDlg : public CDialog
{
public:
    CPCANBasicAdvancedDlg(const CString& omChannelName, BOOL bListenOnly, CWnd* pParent = nullptr);
    enum { IDD = IDD_PCAN_ADVANCED };
    BOOL m_bListenOnly;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    CString m_omChannelName;
};
