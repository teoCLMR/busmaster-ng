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
* \file           CAN_SHC31/CAN_SHC31_Extern.h
* \brief          Exports API functions for the DSD SH-C31x (gs_usb) CAN hardware interface
*
* Exports API functions for the DSD SH-C31x (gs_usb) CAN hardware interface.
* Follows the BusMaster DIL vendor-DLL contract: one C function
* GetIDIL_CAN_Controller() returning a CBaseDIL_CAN_Controller instance.
*/

#ifndef __INCCAN_SHC31_Extern
#define __INCCAN_SHC31_Extern

#if defined USAGEMODE
    #undef USAGEMODE
#endif

#if defined USAGE_EXPORT
    #define USAGEMODE   __declspec(dllexport)
#else
    #define USAGEMODE   __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface);

#ifdef __cplusplus
}
#endif

#endif
