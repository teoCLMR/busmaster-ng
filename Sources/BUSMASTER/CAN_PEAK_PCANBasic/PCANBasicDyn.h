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
 * \file      PCANBasicDyn.h
 * \brief     The part of the PEAK PCAN-Basic API used by the CAN_PEAK_PCANBasic driver
 *
 * BUSMASTER-NG: PCANBasic.dll is installed by the PEAK device driver package, so it is loaded at run time with
 * LoadLibrary and nothing of PEAK is needed to build. Only the types, constants and functions used here are declared;
 * the values are those of the public PCAN-Basic API.
 */

#pragma once

#include <windows.h>

typedef WORD  TPCANHandle;
typedef DWORD TPCANStatus;
typedef BYTE  TPCANParameter;
typedef BYTE  TPCANMessageType;
typedef BYTE  TPCANType;
typedef WORD  TPCANBaudrate;

/* Channel handles */
#define PCAN_NONEBUS            0x00
#define PCAN_PCIBUS1            0x41    /* PCIBUS1..8 = 0x41..0x48, PCIBUS9..16 = 0x409..0x410 */
#define PCAN_USBBUS1            0x51    /* USBBUS1..8 = 0x51..0x58, USBBUS9..16 = 0x509..0x510 */
#define PCAN_PCCBUS1            0x61
#define PCAN_PCCBUS2            0x62
#define PCAN_LANBUS1            0x801   /* LANBUS1..16 = 0x801..0x810 */

/* Status codes */
#define PCAN_ERROR_OK           0x00000
#define PCAN_ERROR_XMTFULL      0x00001
#define PCAN_ERROR_OVERRUN      0x00002
#define PCAN_ERROR_BUSLIGHT     0x00004
#define PCAN_ERROR_BUSHEAVY     0x00008
#define PCAN_ERROR_BUSWARNING   PCAN_ERROR_BUSHEAVY
#define PCAN_ERROR_BUSPASSIVE   0x40000
#define PCAN_ERROR_BUSOFF       0x00010
#define PCAN_ERROR_QRCVEMPTY    0x00020
#define PCAN_ERROR_QOVERRUN     0x00040
#define PCAN_ERROR_QXMTFULL     0x00080
#define PCAN_ERROR_NODRIVER     0x00200
#define PCAN_ERROR_HWINUSE      0x00400
#define PCAN_ERROR_NETINUSE     0x00800
#define PCAN_ERROR_ILLHW        0x01400
#define PCAN_ERROR_INITIALIZE   0x4000000

/* Message types */
#define PCAN_MESSAGE_STANDARD   0x00
#define PCAN_MESSAGE_RTR        0x01
#define PCAN_MESSAGE_EXTENDED   0x02
#define PCAN_MESSAGE_FD         0x04
#define PCAN_MESSAGE_ECHO       0x20
#define PCAN_MESSAGE_ERRFRAME   0x40
#define PCAN_MESSAGE_STATUS     0x80

/* Bit rates (BTR0/BTR1 of an SJA1000 with a 16 MHz clock) */
#define PCAN_BAUD_1M            0x0014
#define PCAN_BAUD_800K          0x0016
#define PCAN_BAUD_500K          0x001C
#define PCAN_BAUD_250K          0x011C
#define PCAN_BAUD_125K          0x031C
#define PCAN_BAUD_100K          0x432F
#define PCAN_BAUD_95K           0xC34E
#define PCAN_BAUD_83K           0x852B
#define PCAN_BAUD_50K           0x472F
#define PCAN_BAUD_47K           0x1414
#define PCAN_BAUD_33K           0x8B2F
#define PCAN_BAUD_20K           0x532F
#define PCAN_BAUD_10K           0x672F
#define PCAN_BAUD_5K            0x7F7F

/* Parameters */
#define PCAN_DEVICE_ID          0x01
#define PCAN_RECEIVE_EVENT      0x03
#define PCAN_API_VERSION        0x05
#define PCAN_CHANNEL_VERSION    0x06
#define PCAN_LISTEN_ONLY        0x08
#define PCAN_CHANNEL_CONDITION  0x0D
#define PCAN_HARDWARE_NAME      0x0E
#define PCAN_CONTROLLER_NUMBER  0x10
#define PCAN_ALLOW_STATUS_FRAMES 0x1E
#define PCAN_ALLOW_ERROR_FRAMES 0x20
#define PCAN_FIRMWARE_VERSION   0x29
#define PCAN_ATTACHED_CHANNELS_COUNT 0x2A   /* PCAN-Basic 4.2 and later */
#define PCAN_ATTACHED_CHANNELS  0x2B

#define FEATURE_FD_CAPABLE      0x01

#define PCAN_PARAMETER_OFF      0x00
#define PCAN_PARAMETER_ON       0x01

/* Values of PCAN_CHANNEL_CONDITION */
#define PCAN_CHANNEL_UNAVAILABLE 0x00
#define PCAN_CHANNEL_AVAILABLE  0x01
#define PCAN_CHANNEL_OCCUPIED   0x02
#define PCAN_CHANNEL_PCANVIEW   0x03    /* in use by PCAN-View, can be connected as well */

#define MAX_LENGTH_HARDWARE_NAME  33
#define MAX_LENGTH_VERSION_STRING 256

typedef struct tagTPCANMsg
{
    DWORD             ID;
    TPCANMessageType  MSGTYPE;
    BYTE              LEN;
    BYTE              DATA[8];
} TPCANMsg;

/* One entry of PCAN_ATTACHED_CHANNELS (natural alignment, 52 bytes) */
typedef struct tagTPCANChannelInformation
{
    TPCANHandle channel_handle;
    BYTE        device_type;
    BYTE        controller_number;
    DWORD       device_features;
    char        device_name[MAX_LENGTH_HARDWARE_NAME];
    DWORD       device_id;
    DWORD       channel_condition;
} TPCANChannelInformation;
static_assert(sizeof(TPCANChannelInformation) == 52, "layout of TPCANChannelInformation as in PCANBasic.h");

typedef struct tagTPCANTimestamp
{
    DWORD millis;
    WORD  millis_overflow;
    WORD  micros;
} TPCANTimestamp;

typedef TPCANStatus (__stdcall* PFN_PCAN_INITIALIZE)(TPCANHandle, TPCANBaudrate, TPCANType, DWORD, WORD);
typedef TPCANStatus (__stdcall* PFN_PCAN_UNINITIALIZE)(TPCANHandle);
typedef TPCANStatus (__stdcall* PFN_PCAN_GETSTATUS)(TPCANHandle);
typedef TPCANStatus (__stdcall* PFN_PCAN_READ)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
typedef TPCANStatus (__stdcall* PFN_PCAN_WRITE)(TPCANHandle, TPCANMsg*);
typedef TPCANStatus (__stdcall* PFN_PCAN_GETVALUE)(TPCANHandle, TPCANParameter, void*, DWORD);
typedef TPCANStatus (__stdcall* PFN_PCAN_SETVALUE)(TPCANHandle, TPCANParameter, void*, DWORD);
typedef TPCANStatus (__stdcall* PFN_PCAN_GETERRORTEXT)(TPCANStatus, WORD, LPSTR);
