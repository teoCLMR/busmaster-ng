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
 * \file           Shc31ChannelSettings.cpp
 * \brief          Driver-owned per-channel settings dialog for the DSD SH-C31x
 *
 * \section DESCRIPTION
 * MFC implementation of the IChangeRegisters dialog opened by the Advanced
 * button of the standard CHardwareListingCAN dialog. It edits exactly one
 * logical channel (SCONTROLLER_DETAILS at the selected-list index): CAN FD
 * enable, nominal and data-phase bitrate plus sample point, listen-only, and
 * the adapter identify toggle. The achieved timing is recomputed with the pure
 * Shc31TimingCalculate calculator (170 MHz FDCAN clock) and shown read-only,
 * with a warning when only the nearest achievable rate exists (800k nominal,
 * 4M/8M data). All writes happen on OK only.
 */

#include "CAN_SHC31_stdafx.h"
#include "Shc31ChannelSettings.h"
#include "Shc31BitTiming.h"
#include "BaseDIL_CAN_Controller.h"

#include <stdlib.h>
#include <stdio.h>
#include <string>

/** Accepted nominal (arbitration) bitrate range, bit/s */
#define SHC31_NOMINAL_BITRATE_MIN                   10000U

/** Accepted nominal (arbitration) bitrate range, bit/s */
#define SHC31_NOMINAL_BITRATE_MAX                   1000000U

/** Accepted data-phase bitrate range, bit/s (TJA1051 5 Mbit ceiling) */
#define SHC31_DATA_BITRATE_MIN                      100000U

/** Accepted data-phase bitrate range, bit/s (TJA1051 5 Mbit ceiling) */
#define SHC31_DATA_BITRATE_MAX                      5000000U

/** Accepted nominal sample point range, percent */
#define SHC31_NOMINAL_SP_MIN                        50.0

/** Accepted nominal sample point range, percent */
#define SHC31_NOMINAL_SP_MAX                        99.0

/** Accepted data-phase sample point range, integer percent */
#define SHC31_DATA_SP_MIN                           50U

/** Accepted data-phase sample point range, integer percent */
#define SHC31_DATA_SP_MAX                           99U

/** Default nominal bitrate for a channel with no saved configuration */
#define SHC31_DEFAULT_NOMINAL_RATE                  500000U

/** Default nominal sample point for a channel with no saved configuration */
#define SHC31_DEFAULT_NOMINAL_SP                    87.5

/** Default data-phase bitrate for a channel with no saved configuration */
#define SHC31_DEFAULT_DATA_RATE                     2000000U

/** Default data-phase sample point (integer percent) */
#define SHC31_DEFAULT_DATA_SP_PERCENT               75U

/**
 * \brief         Converts a decimal percent sample point to basis points
 *
 * \param dPercent  Sample point in percent (for example 87.5)
 *
 * \return        Sample point in 0.01 percent units (for example 8750)
 */
uint32_t Shc31PercentToBp(double dPercent)
{
    uint32_t unBp = 0U;
    double dValue = dPercent * 100.0;

    if (dValue >= 0.0)
    {
        unBp = (uint32_t)(dValue + 0.5);
    }

    return unBp;
}

/**
 * \brief         Formats a timing result for the read-only summary controls
 *
 * \param sTiming  Calculated timing
 * \param eStatus  Calculator status for the same candidate
 * \param pomStr   Output string
 *
 * \return        None
 */
void Shc31FormatTiming(const Shc31TimingStruct& sTiming, Shc31TimingStatusEnum eStatus, CString* pomStr)
{
    if (pomStr != nullptr)
    {
        if ((eStatus == SHC31_TIMING_ERR_PARAM) || (eStatus == SHC31_TIMING_ERR_NOT_ACHIEVABLE))
        {
            *pomStr = "not achievable";
        }
        else
        {
            pomStr->Format("%lu bit/s\n%.2f %%\n%s",
                           (unsigned long)sTiming.AchievedBitrate,
                           ((double)sTiming.SamplePointBp) / 100.0,
                           (eStatus == SHC31_TIMING_OK) ? "exact" : "nearest");
        }
    }
}

/**
 * \brief         Settings dialog: one logical channel at a time
 */
class CShc31SettingsDlg : public CDialog
{
public:
    enum { IDD = IDD_SHC31_CHANNEL_SETTINGS };

    CShc31SettingsDlg(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool bFdCapable, bool bIdentifyCapable, CWnd* pParent);

    bool m_bFdEnabled;
    UINT m_unNominalBitrate;
    double m_dNominalSp;
    UINT m_unDataBitrate;
    UINT m_unDataSp;
    bool m_bListenOnly;
    bool m_bFdCapable;
    bool m_bIdentifyCapable;

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();

    afx_msg void OnBnClickedFd();
    afx_msg void OnBnClickedIdentify();
    afx_msg void OnTimingChanged(UINT nID);

    DECLARE_MESSAGE_MAP()

private:
    void vEnableDataControls();
    void vUpdateSummary();
    void vStopIdentify();
    bool bReadInputs(bool bShowErrors, UINT* punNominalRate, double* pdNominalSp, UINT* punDataRate, UINT* punDataSp);

    CDIL_CAN_SHC31* m_pDriver;
    std::string m_strDescription;
    bool m_bIdentifying;
};

BEGIN_MESSAGE_MAP(CShc31SettingsDlg, CDialog)
    ON_BN_CLICKED(IDC_SHC31_CHK_FD, &CShc31SettingsDlg::OnBnClickedFd)
    ON_BN_CLICKED(IDC_SHC31_CHK_IDENTIFY, &CShc31SettingsDlg::OnBnClickedIdentify)
    ON_CONTROL_RANGE(EN_CHANGE, IDC_SHC31_EDIT_NOM_BITRATE, IDC_SHC31_EDIT_DATA_SP, &CShc31SettingsDlg::OnTimingChanged)
END_MESSAGE_MAP()

/**
 * \brief         Constructs the settings dialog
 *
 * \param pDriver            Driver owning the enumerated devices
 * \param strDescription     Persistence description of the channel's adapter
 * \param bFdCapable         Adapter advertises GS_CAN_FEATURE_FD
 * \param bIdentifyCapable   Adapter advertises GS_CAN_FEATURE_IDENTIFY
 * \param pParent            Parent window
 *
 * \return        None
 */
CShc31SettingsDlg::CShc31SettingsDlg(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool bFdCapable, bool bIdentifyCapable, CWnd* pParent) :
    CDialog(CShc31SettingsDlg::IDD, pParent),
    m_bFdEnabled(false),
    m_unNominalBitrate(SHC31_DEFAULT_NOMINAL_RATE),
    m_dNominalSp(SHC31_DEFAULT_NOMINAL_SP),
    m_unDataBitrate(SHC31_DEFAULT_DATA_RATE),
    m_unDataSp(SHC31_DEFAULT_DATA_SP_PERCENT),
    m_bListenOnly(false),
    m_bFdCapable(bFdCapable),
    m_bIdentifyCapable(bIdentifyCapable),
    m_pDriver(pDriver),
    m_strDescription(strDescription),
    m_bIdentifying(false)
{
}

/**
 * \brief         Initialises the dialog controls from the channel values
 *
 * \param         None
 *
 * \return        TRUE when the dialog keeps the default focus behaviour
 */
BOOL CShc31SettingsDlg::OnInitDialog()
{
    char acBuffer[32] = { '\0' };
    CWnd* pControl = nullptr;

    CDialog::OnInitDialog();

    SetDlgItemText(IDC_SHC31_STATIC_DEVICE, m_strDescription.c_str());

    (void)sprintf_s(acBuffer, sizeof(acBuffer), "%u", m_unNominalBitrate);
    SetDlgItemText(IDC_SHC31_EDIT_NOM_BITRATE, acBuffer);
    (void)sprintf_s(acBuffer, sizeof(acBuffer), "%g", m_dNominalSp);
    SetDlgItemText(IDC_SHC31_EDIT_NOM_SP, acBuffer);
    (void)sprintf_s(acBuffer, sizeof(acBuffer), "%u", m_unDataBitrate);
    SetDlgItemText(IDC_SHC31_EDIT_DATA_BITRATE, acBuffer);
    (void)sprintf_s(acBuffer, sizeof(acBuffer), "%u", m_unDataSp);
    SetDlgItemText(IDC_SHC31_EDIT_DATA_SP, acBuffer);

    CheckDlgButton(IDC_SHC31_CHK_FD, (m_bFdEnabled == true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_SHC31_CHK_LISTEN_ONLY, (m_bListenOnly == true) ? BST_CHECKED : BST_UNCHECKED);

    if (m_bFdCapable == false)
    {
        CheckDlgButton(IDC_SHC31_CHK_FD, BST_UNCHECKED);
        m_bFdEnabled = false;

        pControl = GetDlgItem(IDC_SHC31_CHK_FD);

        if (pControl != nullptr)
        {
            pControl->EnableWindow(FALSE);
        }
    }

    CheckDlgButton(IDC_SHC31_CHK_IDENTIFY, BST_UNCHECKED);

    if (m_bIdentifyCapable == true)
    {
        SetDlgItemText(IDC_SHC31_STATIC_IDENTIFY, "Not identifying");
    }
    else
    {
        pControl = GetDlgItem(IDC_SHC31_CHK_IDENTIFY);

        if (pControl != nullptr)
        {
            pControl->EnableWindow(FALSE);
        }

        SetDlgItemText(IDC_SHC31_STATIC_IDENTIFY, "Not supported by this adapter");
    }

    vEnableDataControls();
    vUpdateSummary();

    return TRUE;
}

/**
 * \brief         Reads and validates the controls
 *
 * \param bShowErrors    TRUE to show a message box for the first bad field
 * \param punNominalRate Output: nominal bitrate in bit/s
 * \param pdNominalSp    Output: nominal sample point in percent
 * \param punDataRate    Output: data-phase bitrate in bit/s
 * \param punDataSp      Output: data-phase sample point in integer percent
 *
 * \return        true when every relevant field is valid
 */
bool CShc31SettingsDlg::bReadInputs(bool bShowErrors, UINT* punNominalRate, double* pdNominalSp, UINT* punDataRate, UINT* punDataSp)
{
    bool bValid = false;

    if ((punNominalRate != nullptr) && (pdNominalSp != nullptr) && (punDataRate != nullptr) && (punDataSp != nullptr))
    {
        BOOL bTranslated = FALSE;
        UINT unNominalRate = GetDlgItemInt(IDC_SHC31_EDIT_NOM_BITRATE, &bTranslated, FALSE);

        if ((bTranslated == FALSE) || (unNominalRate < SHC31_NOMINAL_BITRATE_MIN) || (unNominalRate > SHC31_NOMINAL_BITRATE_MAX))
        {
            if (bShowErrors == true)
            {
                AfxMessageBox("The nominal bitrate must be a whole number between 10000 and 1000000 bit/s.", MB_OK | MB_ICONERROR);
            }
        }
        else
        {
            CString omStrNominalSp;
            GetDlgItemText(IDC_SHC31_EDIT_NOM_SP, omStrNominalSp);
            double dNominalSp = strtod((LPCTSTR)omStrNominalSp, nullptr);

            if ((dNominalSp < SHC31_NOMINAL_SP_MIN) || (dNominalSp > SHC31_NOMINAL_SP_MAX))
            {
                if (bShowErrors == true)
                {
                    AfxMessageBox("The nominal sample point must be between 50.0 and 99.0 percent.", MB_OK | MB_ICONERROR);
                }
            }
            else
            {
                UINT unDataRate = SHC31_DEFAULT_DATA_RATE;
                UINT unDataSp = SHC31_DEFAULT_DATA_SP_PERCENT;
                bool bDataValid = true;

                if (m_bFdEnabled == true)
                {
                    unDataRate = GetDlgItemInt(IDC_SHC31_EDIT_DATA_BITRATE, &bTranslated, FALSE);

                    if ((bTranslated == FALSE) || (unDataRate < SHC31_DATA_BITRATE_MIN) || (unDataRate > SHC31_DATA_BITRATE_MAX))
                    {
                        if (bShowErrors == true)
                        {
                            AfxMessageBox("The data bitrate must be a whole number between 100000 and 5000000 bit/s.", MB_OK | MB_ICONERROR);
                        }

                        bDataValid = false;
                    }
                    else
                    {
                        unDataSp = GetDlgItemInt(IDC_SHC31_EDIT_DATA_SP, &bTranslated, FALSE);

                        if ((bTranslated == FALSE) || (unDataSp < SHC31_DATA_SP_MIN) || (unDataSp > SHC31_DATA_SP_MAX))
                        {
                            if (bShowErrors == true)
                            {
                                AfxMessageBox("The data sample point must be an integer between 50 and 99 percent.", MB_OK | MB_ICONERROR);
                            }

                            bDataValid = false;
                        }
                    }
                }

                if (bDataValid == true)
                {
                    *punNominalRate = unNominalRate;
                    *pdNominalSp = dNominalSp;
                    *punDataRate = unDataRate;
                    *punDataSp = unDataSp;
                    bValid = true;
                }
            }
        }
    }

    return bValid;
}

/**
 * \brief         Enables the data-phase fields only when FD is active
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::vEnableDataControls()
{
    BOOL bEnable = ((m_bFdEnabled == true) && (m_bFdCapable == true)) ? TRUE : FALSE;
    CWnd* pControl = GetDlgItem(IDC_SHC31_EDIT_DATA_BITRATE);

    if (pControl != nullptr)
    {
        pControl->EnableWindow(bEnable);
    }

    pControl = GetDlgItem(IDC_SHC31_EDIT_DATA_SP);

    if (pControl != nullptr)
    {
        pControl->EnableWindow(bEnable);
    }
}

/**
 * \brief         Recomputes the achieved timing summaries (live update)
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::vUpdateSummary()
{
    UINT unNominalRate = 0U;
    UINT unDataRate = 0U;
    UINT unDataSp = 0U;
    double dNominalSp = 0.0;

    if (bReadInputs(false, &unNominalRate, &dNominalSp, &unDataRate, &unDataSp) == false)
    {
        SetDlgItemText(IDC_SHC31_STATIC_NOM_ACHIEVED, "invalid input");
        SetDlgItemText(IDC_SHC31_STATIC_DATA_ACHIEVED, (m_bFdEnabled == true) ? "invalid input" : "disabled");
        SetDlgItemText(IDC_SHC31_STATIC_WARNING, "");
    }
    else
    {
        Shc31TimingStruct sNominal;
        Shc31TimingStatusEnum eNominal = Shc31TimingCalculate(unNominalRate, Shc31PercentToBp(dNominalSp), SHC31_PHASE_NOMINAL, &sNominal);
        CString omStrNominal;
        CString omWarning = "";

        Shc31FormatTiming(sNominal, eNominal, &omStrNominal);
        SetDlgItemText(IDC_SHC31_STATIC_NOM_ACHIEVED, omStrNominal);

        if (eNominal == SHC31_TIMING_OK_NEAREST)
        {
            omWarning.Format("Nominal %lu bit/s is not exact; nearest is %lu bit/s.",
                             (unsigned long)unNominalRate, (unsigned long)sNominal.AchievedBitrate);
        }

        if (m_bFdEnabled == true)
        {
            Shc31TimingStruct sData;
            Shc31TimingStatusEnum eData = Shc31TimingCalculate(unDataRate, unDataSp * 100U, SHC31_PHASE_DATA, &sData);
            CString omStrData;

            Shc31FormatTiming(sData, eData, &omStrData);
            SetDlgItemText(IDC_SHC31_STATIC_DATA_ACHIEVED, omStrData);

            if (eData == SHC31_TIMING_OK_NEAREST)
            {
                CString omDataWarning;
                omDataWarning.Format(" Data %lu bit/s is not exact; nearest is %lu bit/s.",
                                     (unsigned long)unDataRate, (unsigned long)sData.AchievedBitrate);
                omWarning += omDataWarning;
            }
        }
        else
        {
            SetDlgItemText(IDC_SHC31_STATIC_DATA_ACHIEVED, "disabled");
        }

        SetDlgItemText(IDC_SHC31_STATIC_WARNING, omWarning);
    }
}

/**
 * \brief         Cancels an active identify blink before the dialog closes
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::vStopIdentify()
{
    if ((m_bIdentifying == true) && (m_bIdentifyCapable == true))
    {
        (void)Shc31SendIdentifyByDescription(m_pDriver, m_strDescription, false);
        m_bIdentifying = false;
    }
}

/**
 * \brief         Applies the OK-time validation, warnings and result capture
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::OnOK()
{
    UINT unNominalRate = 0U;
    UINT unDataRate = 0U;
    UINT unDataSp = 0U;
    double dNominalSp = 0.0;

    m_bFdEnabled = (IsDlgButtonChecked(IDC_SHC31_CHK_FD) == BST_CHECKED) ? true : false;

    if (bReadInputs(true, &unNominalRate, &dNominalSp, &unDataRate, &unDataSp) == false)
    {
        return;
    }

    Shc31TimingStruct sNominal;
    Shc31TimingStatusEnum eNominal = Shc31TimingCalculate(unNominalRate, Shc31PercentToBp(dNominalSp), SHC31_PHASE_NOMINAL, &sNominal);

    if ((eNominal == SHC31_TIMING_ERR_PARAM) || (eNominal == SHC31_TIMING_ERR_NOT_ACHIEVABLE))
    {
        AfxMessageBox("The nominal timing cannot be achieved with the device constraint set.", MB_OK | MB_ICONERROR);
        return;
    }

    CString omWarning = "";

    if (eNominal == SHC31_TIMING_OK_NEAREST)
    {
        omWarning.Format("Nominal %lu bit/s is not exactly achievable; the nearest is %lu bit/s at %.2f %%.",
                         (unsigned long)unNominalRate, (unsigned long)sNominal.AchievedBitrate,
                         ((double)sNominal.SamplePointBp) / 100.0);
    }

    if (m_bFdEnabled == true)
    {
        Shc31TimingStruct sData;
        Shc31TimingStatusEnum eData = Shc31TimingCalculate(unDataRate, unDataSp * 100U, SHC31_PHASE_DATA, &sData);

        if ((eData == SHC31_TIMING_ERR_PARAM) || (eData == SHC31_TIMING_ERR_NOT_ACHIEVABLE))
        {
            AfxMessageBox("The data-phase timing cannot be achieved with the device constraint set.", MB_OK | MB_ICONERROR);
            return;
        }

        if (eData == SHC31_TIMING_OK_NEAREST)
        {
            CString omDataWarning;
            omDataWarning.Format(" Data %lu bit/s is not exactly achievable; the nearest is %lu bit/s at %.2f %%.",
                                 (unsigned long)unDataRate, (unsigned long)sData.AchievedBitrate,
                                 ((double)sData.SamplePointBp) / 100.0);
            omWarning += omDataWarning;
        }
    }

    if (omWarning.IsEmpty() == FALSE)
    {
        AfxMessageBox(omWarning, MB_OK | MB_ICONWARNING);
    }

    m_unNominalBitrate = unNominalRate;
    m_dNominalSp = dNominalSp;
    m_unDataBitrate = unDataRate;
    m_unDataSp = unDataSp;
    m_bListenOnly = (IsDlgButtonChecked(IDC_SHC31_CHK_LISTEN_ONLY) == BST_CHECKED) ? true : false;

    vStopIdentify();

    CDialog::OnOK();
}

/**
 * \brief         Cancels the dialog, stopping an active identify blink first
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::OnCancel()
{
    vStopIdentify();
    CDialog::OnCancel();
}

/**
 * \brief         Handles the FD enable checkbox
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::OnBnClickedFd()
{
    m_bFdEnabled = (IsDlgButtonChecked(IDC_SHC31_CHK_FD) == BST_CHECKED) ? true : false;
    vEnableDataControls();
    vUpdateSummary();
}

/**
 * \brief         Handles any of the four timing edit boxes
 *
 * \param nID     Control identifier of the changed edit box
 *
 * \return        None
 */
void CShc31SettingsDlg::OnTimingChanged(UINT nID)
{
    (void)nID;
    vUpdateSummary();
}

/**
 * \brief         Toggles the adapter identify blink
 *
 * \param         None
 *
 * \return        None
 */
void CShc31SettingsDlg::OnBnClickedIdentify()
{
    BOOL bChecked = (IsDlgButtonChecked(IDC_SHC31_CHK_IDENTIFY) == BST_CHECKED) ? TRUE : FALSE;

    if (bChecked == TRUE)
    {
        HRESULT hResult = Shc31SendIdentifyByDescription(m_pDriver, m_strDescription, true);

        if (hResult == S_OK)
        {
            m_bIdentifying = true;
            SetDlgItemText(IDC_SHC31_STATIC_IDENTIFY, "Identifying (LEDs blinking)");
        }
        else
        {
            m_bIdentifying = false;
            CheckDlgButton(IDC_SHC31_CHK_IDENTIFY, BST_UNCHECKED);
            SetDlgItemText(IDC_SHC31_STATIC_IDENTIFY, "Identify failed");
        }
    }
    else
    {
        (void)Shc31SendIdentifyByDescription(m_pDriver, m_strDescription, false);
        m_bIdentifying = false;
        SetDlgItemText(IDC_SHC31_STATIC_IDENTIFY, "Not identifying");
    }
}

/**
 * \brief         Constructs the IChangeRegisters adapter object
 *
 * \param pDriver             Driver owning the enumerated devices
 * \param pControllerDetails  Controller details array passed by the caller
 * \param nCount              Number of configured channels
 *
 * \return        None
 */
CShc31ChannelSettings::CShc31ChannelSettings(CDIL_CAN_SHC31* pDriver, PSCONTROLLER_DETAILS pControllerDetails, UINT nCount) :
    m_pDriver(pDriver),
    m_pControllerDetails(pControllerDetails),
    m_nCount(nCount)
{
}

/**
 * \brief         Destroys the IChangeRegisters adapter object
 *
 * \param         None
 *
 * \return        None
 */
CShc31ChannelSettings::~CShc31ChannelSettings()
{
}

/**
 * \brief         Opens the per-channel settings dialog for the highlighted
 *                selected channel
 *
 * \param pControllerDetails  Controller details array
 * \param nCount              Number of configured channels
 * \param nSelectedHw         Index into the selected list (the channel to edit)
 *
 * \return        INFO_INITDAT_CONFIRM_CONFIG when applied, otherwise
 *                WARN_INITDAT_NCONFIRM
 */
int CShc31ChannelSettings::InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelectedHw)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    int nResult = WARN_INITDAT_NCONFIRM;

    if ((pControllerDetails != nullptr) && (nSelectedHw < nCount))
    {
        SCONTROLLER_DETAILS& sDetails = pControllerDetails[nSelectedHw];
        bool bFdCapable = false;
        bool bIdentifyCapable = false;

        (void)Shc31GetDeviceInfo(m_pDriver, sDetails.m_omHardwareDesc, &bFdCapable, &bIdentifyCapable);

        bool bFieldsEmpty = (sDetails.m_omStrBaudrate.empty() == true);
        UINT unNominalRate = SHC31_DEFAULT_NOMINAL_RATE;
        double dNominalSp = SHC31_DEFAULT_NOMINAL_SP;
        UINT unDataRate = SHC31_DEFAULT_DATA_RATE;
        UINT unDataSp = SHC31_DEFAULT_DATA_SP_PERCENT;

        if (bFieldsEmpty == false)
        {
            unNominalRate = (UINT)strtoul(sDetails.m_omStrBaudrate.c_str(), nullptr, 10);
            dNominalSp = strtod(sDetails.m_omStrSamplePercentage.c_str(), nullptr);
            unDataRate = sDetails.m_unDataBitRate;
            unDataSp = sDetails.m_unDataSamplePoint;

            if ((unNominalRate < SHC31_NOMINAL_BITRATE_MIN) || (unNominalRate > SHC31_NOMINAL_BITRATE_MAX))
            {
                unNominalRate = SHC31_DEFAULT_NOMINAL_RATE;
            }

            if ((dNominalSp < SHC31_NOMINAL_SP_MIN) || (dNominalSp > SHC31_NOMINAL_SP_MAX))
            {
                dNominalSp = SHC31_DEFAULT_NOMINAL_SP;
            }

            if ((unDataRate < SHC31_DATA_BITRATE_MIN) || (unDataRate > SHC31_DATA_BITRATE_MAX))
            {
                unDataRate = SHC31_DEFAULT_DATA_RATE;
            }

            if ((unDataSp < SHC31_DATA_SP_MIN) || (unDataSp > SHC31_DATA_SP_MAX))
            {
                unDataSp = SHC31_DEFAULT_DATA_SP_PERCENT;
            }
        }

        CShc31SettingsDlg sDlg(m_pDriver, sDetails.m_omHardwareDesc, bFdCapable, bIdentifyCapable, CWnd::GetActiveWindow());

        sDlg.m_unNominalBitrate = unNominalRate;
        sDlg.m_dNominalSp = dNominalSp;
        sDlg.m_unDataBitrate = unDataRate;
        sDlg.m_unDataSp = unDataSp;
        sDlg.m_bFdEnabled = (bFieldsEmpty == true) ? bFdCapable : ((sDetails.m_bcanFDEnabled == true) && (bFdCapable == true));
        sDlg.m_bListenOnly = (bFieldsEmpty == true) ? false : (((sDetails.m_ucControllerMode == defMODE_PASSIVE) || (sDetails.m_bPassiveMode != 0)) ? true : false);

        if (sDlg.DoModal() == IDOK)
        {
            char acBuffer[32] = { '\0' };

            sDetails.m_bcanFDEnabled = (sDlg.m_bFdEnabled == true) ? true : false;
            (void)sprintf_s(acBuffer, sizeof(acBuffer), "%u", sDlg.m_unNominalBitrate);
            sDetails.m_omStrBaudrate = acBuffer;
            (void)sprintf_s(acBuffer, sizeof(acBuffer), "%g", sDlg.m_dNominalSp);
            sDetails.m_omStrSamplePercentage = acBuffer;
            sDetails.m_unDataBitRate = sDlg.m_unDataBitrate;
            sDetails.m_unDataSamplePoint = sDlg.m_unDataSp;

            if (sDlg.m_bListenOnly == true)
            {
                sDetails.m_ucControllerMode = (unsigned short)defMODE_PASSIVE;
                sDetails.m_bPassiveMode = 1;
            }
            else
            {
                sDetails.m_ucControllerMode = (unsigned short)defMODE_ACTIVE;
                sDetails.m_bPassiveMode = 0;
            }

            nResult = INFO_INITDAT_CONFIRM_CONFIG;
        }
    }

    return nResult;
}

/**
 * \brief         Validates and normalises the nominal bitrate used by the
 *                standard selection dialog
 *
 * \param dBaudRate  Nominal bitrate in bit/s
 * \param nChannel   Channel index (unused)
 * \param unParam    Extra parameter (unused)
 *
 * \return        The input rate when it lies in 10000..1000000 bit/s,
 *                otherwise 500000 bit/s
 */
double CShc31ChannelSettings::vValidateBaudRate(double dBaudRate, int nChannel, UINT unParam)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    double dResult = (double)SHC31_DEFAULT_NOMINAL_RATE;

    if ((dBaudRate >= (double)SHC31_NOMINAL_BITRATE_MIN) && (dBaudRate <= (double)SHC31_NOMINAL_BITRATE_MAX))
    {
        dResult = dBaudRate;
    }

    (void)nChannel;
    (void)unParam;

    return dResult;
}
