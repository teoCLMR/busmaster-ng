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
 * \file           Shc31ChannelSettings.h
 * \brief          Driver-owned per-channel settings dialog for the DSD SH-C31x
 *
 * \section DESCRIPTION
 * Implements IChangeRegisters for the CAN_SHC31 DIL. The standard hardware
 * selection dialog (CHardwareListingCAN) calls InvokeAdavancedSettings when the
 * user presses Advanced; this class opens a driver-owned MFC dialog that
 * configures exactly one logical channel: CAN FD enable, nominal and data-phase
 * bitrate plus sample point, listen-only, and the adapter identify toggle.
 *
 * The dialog reads the physical adapter from
 * SCONTROLLER_DETAILS::m_omHardwareDesc (the stable device description used
 * for persistence) and queries the driver through the narrow accessors below,
 * so the driver's device table stays private to CAN_SHC31.cpp.
 *
 */

#ifndef __INCShc31ChannelSettings
#define __INCShc31ChannelSettings

#include "DIL_Interface/HardwareListingCAN.h"
#include "Shc31Resource.h"

#include <string>

/** Driver class forward declaration (defined in CAN_SHC31.cpp) */
class CDIL_CAN_SHC31;

/**
 * Resolves an adapter by its persistence description and returns the two
 * capability flags the dialog needs.
 *
 * \param pDriver            Driver instance owning the enumerated devices
 * \param strDescription     Value to match against the device description
 * \param pbFdCapable        Output: device advertises GS_CAN_FEATURE_FD
 * \param pbIdentifyCapable  Output: device advertises GS_CAN_FEATURE_IDENTIFY
 *
 * \return true when a matching device was found
 */
bool Shc31GetDeviceInfo(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool* pbFdCapable, bool* pbIdentifyCapable);

/**
 * Starts or stops the gs_usb LED identify blink on the adapter identified by
 * its persistence description. Works on open and closed devices (the driver
 * opens WinUSB temporarily when the channel is closed).
 *
 * \param pDriver         Driver instance owning the enumerated devices
 * \param strDescription  Value to match against the device description
 * \param bOn             true to start blinking, false to stop
 *
 * \return S_OK on success, S_FALSE when the description does not match or the
 *         request fails
 */
HRESULT Shc31SendIdentifyByDescription(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool bOn);

/**
 * IChangeRegisters implementation handed to CHardwareListingCAN.
 */
class CShc31ChannelSettings : public IChangeRegisters
{
public:
    CShc31ChannelSettings(CDIL_CAN_SHC31* pDriver, PSCONTROLLER_DETAILS pControllerDetails, UINT nCount);
    virtual ~CShc31ChannelSettings();

    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelectedHw);
    double vValidateBaudRate(double dBaudRate, int nChannel, UINT unParam);

private:
    CDIL_CAN_SHC31* m_pDriver;                   /** Driver owning the device table */
    PSCONTROLLER_DETAILS m_pControllerDetails;   /** Last controller details array (informational) */
    UINT m_nCount;                               /** Last channel count (informational) */
};

#endif
