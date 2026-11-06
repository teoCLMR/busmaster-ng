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
 * \file           Shc31Resource.h
 * \brief          Resource identifiers for the CAN_SHC31 channel settings dialog
 *
 * \section DESCRIPTION
 * Identifiers used by the driver-owned per-channel settings dialog
 * (IDD_SHC31_CHANNEL_SETTINGS) and its controls. The dialog template lives in
 * CAN_SHC31.rc. Values start at 2001 so they cannot collide with the shared
 * channel-selection resources (1000 to 1226).
 */

#ifndef __INCShc31Resource
#define __INCShc31Resource

/** Per-channel CAN FD and bit-timing settings dialog template */
#define IDD_SHC31_CHANNEL_SETTINGS              2001

/** Enable CAN FD checkbox */
#define IDC_SHC31_CHK_FD                        2002

/** Nominal (arbitration) bitrate edit, bit/s */
#define IDC_SHC31_EDIT_NOM_BITRATE              2003

/** Nominal sample point edit, decimal percent */
#define IDC_SHC31_EDIT_NOM_SP                   2004

/** Data-phase bitrate edit, bit/s */
#define IDC_SHC31_EDIT_DATA_BITRATE             2005

/** Data-phase sample point edit, integer percent */
#define IDC_SHC31_EDIT_DATA_SP                  2006

/** Listen-only (passive) checkbox */
#define IDC_SHC31_CHK_LISTEN_ONLY               2007

/** Read-only achieved nominal timing summary */
#define IDC_SHC31_STATIC_NOM_ACHIEVED           2008

/** Read-only achieved data-phase timing summary */
#define IDC_SHC31_STATIC_DATA_ACHIEVED          2009

/** Identify (blink LEDs) checkbox */
#define IDC_SHC31_CHK_IDENTIFY                  2010

/** Read-only identify status */
#define IDC_SHC31_STATIC_IDENTIFY               2011

/** Read-only adapter description */
#define IDC_SHC31_STATIC_DEVICE                 2012

/** Nearest-achievable warning line */
#define IDC_SHC31_STATIC_WARNING                2013

#endif
