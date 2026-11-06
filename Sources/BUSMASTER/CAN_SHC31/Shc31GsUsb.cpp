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
 * \file           Shc31GsUsb.cpp
 * \brief          Pure gs_usb wire codec for the DSD SH-C31x (gs_usb) driver
 *
 * \section DESCRIPTION
 * Implementation of the gs_usb frame codec, DLC maps and timestamp wrap
 * extension. See Shc31GsUsb.h for the full contract and the verified
 * protocol facts.
 *
 */

#include "Shc31GsUsb.h"

#include <string.h>

/** CAN-FD DLC codes above the classic 8-byte range (the same coding as slcan) */
#define SHC31_GSUSB_FD_DLC_CODE_12B                     9U
#define SHC31_GSUSB_FD_DLC_CODE_16B                     10U
#define SHC31_GSUSB_FD_DLC_CODE_20B                     11U
#define SHC31_GSUSB_FD_DLC_CODE_24B                     12U
#define SHC31_GSUSB_FD_DLC_CODE_32B                     13U
#define SHC31_GSUSB_FD_DLC_CODE_48B                     14U
#define SHC31_GSUSB_FD_DLC_CODE_64B                     15U

/** Data lengths in bytes for the FD DLC codes 9..15, indexed by
    code - SHC31_GSUSB_FD_DLC_CODE_12B */
const uint32_t SHC31_GSUSB_FD_CODE_TO_LEN[7] =
{
    12U, 16U, 20U, 24U, 32U, 48U, 64U
};

/**
 * Stores a little-endian u32 into a byte buffer.
 *
 * \param punBuf   Output buffer, at least 4 bytes
 * \param unValue  Value to store
 *
 * \return None
 */
void Shc31GsUsbPutU32(uint8_t* punBuf, uint32_t unValue)
{
    punBuf[0] = (uint8_t)(unValue & 0xFFU);
    punBuf[1] = (uint8_t)((unValue >> 8) & 0xFFU);
    punBuf[2] = (uint8_t)((unValue >> 16) & 0xFFU);
    punBuf[3] = (uint8_t)((unValue >> 24) & 0xFFU);
}

/**
 * Reads a little-endian u32 from a byte buffer.
 *
 * \param punBuf  Input buffer, at least 4 bytes
 *
 * \return The read value
 */
uint32_t Shc31GsUsbGetU32(const uint8_t* punBuf)
{
    return (uint32_t)punBuf[0] |
           ((uint32_t)punBuf[1] << 8) |
           ((uint32_t)punBuf[2] << 16) |
           ((uint32_t)punBuf[3] << 24);
}

/**
 * Converts a CAN FD data length in bytes to the FD DLC code.
 *
 * \param unDataLen  Data length in bytes
 *
 * \return DLC code 0..15, or SHC31_GSUSB_FD_DLC_INVALID
 */
uint32_t Shc31GsUsbFdLenToDlc(uint32_t unDataLen)
{
    uint32_t unCode = SHC31_GSUSB_FD_DLC_INVALID;

    if (unDataLen <= 8U)
    {
        unCode = unDataLen;
    }
    else if (12U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_12B;
    }
    else if (16U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_16B;
    }
    else if (20U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_20B;
    }
    else if (24U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_24B;
    }
    else if (32U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_32B;
    }
    else if (48U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_48B;
    }
    else if (64U == unDataLen)
    {
        unCode = SHC31_GSUSB_FD_DLC_CODE_64B;
    }
    else
    {
        unCode = SHC31_GSUSB_FD_DLC_INVALID;
    }

    return unCode;
}

/**
 * Converts an FD DLC code to the data length in bytes.
 *
 * \param unDlcCode  DLC code 0..15
 *
 * \return Data length in bytes 0..64, or SHC31_GSUSB_FD_DLC_INVALID
 */
uint32_t Shc31GsUsbFdDlcToLen(uint32_t unDlcCode)
{
    uint32_t unDataLen = SHC31_GSUSB_FD_DLC_INVALID;

    if (unDlcCode <= 8U)
    {
        unDataLen = unDlcCode;
    }
    else if ((unDlcCode >= SHC31_GSUSB_FD_DLC_CODE_12B) && (unDlcCode <= SHC31_GSUSB_FD_DLC_CODE_64B))
    {
        unDataLen = SHC31_GSUSB_FD_CODE_TO_LEN[unDlcCode - SHC31_GSUSB_FD_DLC_CODE_12B];
    }
    else
    {
        unDataLen = SHC31_GSUSB_FD_DLC_INVALID;
    }

    return unDataLen;
}

/**
 * Encodes one TX frame into the gs_host_frame wire format.
 *
 * \param unMsgId      CAN identifier (11- or 29-bit, masked to its width)
 * \param bExtended    TRUE for a 29-bit identifier
 * \param bRtr         TRUE for a remote frame (classic only, no payload)
 * \param bFd          TRUE for a CAN-FD frame
 * \param bBrs         TRUE to set the BRS flag on an FD frame
 * \param unDataLen    Payload length in bytes
 * \param punData      Payload bytes (may be null when unDataLen is 0 or RTR)
 * \param unEchoId     Echo pool slot for the TX (0..29)
 * \param punFrame     Output frame buffer
 * \param unFrameSize  Output buffer size in bytes
 *
 * \return Encoded frame length in bytes, or 0 when the parameters do not
 *         encode
 */
uint32_t Shc31GsUsbEncodeTxFrame(uint32_t unMsgId, bool bExtended, bool bRtr, bool bFd, bool bBrs, uint32_t unDataLen, const uint8_t* punData, uint32_t unEchoId, uint8_t* punFrame, uint32_t unFrameSize)
{
    uint32_t unFrameLen = 0U;

    if ((punFrame != nullptr) && (unEchoId < SHC31_GSUSB_ECHO_POOL_MAX))
    {
        bool bParamsValid = true;
        uint32_t unIdFlags = 0U;
        uint32_t unFrameFlags = 0U;
        uint32_t unDlc = 0U;
        uint32_t unPayloadLen = 0U;

        if (bExtended == true)
        {
            unIdFlags |= SHC31_GSUSB_ID_FLAG_EFF;

            if ((unMsgId & ~SHC31_GSUSB_ID_EFF_MASK) != 0U)
            {
                bParamsValid = false;
            }
        }
        else
        {
            if ((unMsgId & ~SHC31_GSUSB_ID_SFF_MASK) != 0U)
            {
                bParamsValid = false;
            }
        }

        if (bFd == true)
        {
            unFrameFlags |= SHC31_GSUSB_FRAME_FLAG_FD;

            if (bBrs == true)
            {
                unFrameFlags |= SHC31_GSUSB_FRAME_FLAG_BRS;
            }

            /** FD has no remote frames */
            if (bRtr == true)
            {
                bParamsValid = false;
            }

            unDlc = Shc31GsUsbFdLenToDlc(unDataLen);

            if (unDlc == SHC31_GSUSB_FD_DLC_INVALID)
            {
                bParamsValid = false;
            }
            else
            {
                unPayloadLen = unDataLen;
            }
        }
        else
        {
            if (bBrs == true)
            {
                /** BRS without FD is not a valid combination */
                bParamsValid = false;
            }

            if (unDataLen > 8U)
            {
                bParamsValid = false;
            }
            else
            {
                unPayloadLen = (bRtr == true) ? 0U : unDataLen;
                unDlc = unDataLen;
            }

            if (bRtr == true)
            {
                unIdFlags |= SHC31_GSUSB_ID_FLAG_RTR;
            }
        }

        if (bParamsValid == true)
        {
            /** The firmware arms the bulk OUT endpoint for the full
                gs_host_frame union (80 bytes = 12-byte header + 64 data +
                4 timestamp), so TX frames are always written at full length
                with a zero-filled tail. Writing more than 80 bytes (the
                previous 84) overruns the receive buffer, fails the transfer
                and halts the OUT pipe. */
            unFrameLen = SHC31_GSUSB_FRAME_MAX_SIZE;

            if (unFrameLen <= unFrameSize)
            {
                memset(punFrame, 0, unFrameLen);

                Shc31GsUsbPutU32(punFrame + 0, unEchoId);
                Shc31GsUsbPutU32(punFrame + 4, unMsgId | unIdFlags);
                punFrame[8] = (uint8_t)unDlc;
                punFrame[9] = 0U;
                punFrame[10] = (uint8_t)unFrameFlags;
                punFrame[11] = 0U;

                if ((unPayloadLen > 0U) && (punData != nullptr))
                {
                    memcpy(punFrame + SHC31_GSUSB_FRAME_HEADER_SIZE, punData, unPayloadLen);
                }
            }
            else
            {
                unFrameLen = 0U;
            }
        }
        else
        {
            unFrameLen = 0U;
        }
    }

    return unFrameLen;
}

/**
 * Decodes one gs_host_frame from a bulk RX buffer.
 *
 * \param punFrame  Input buffer (bulk transfer content)
 * \param unAvail   Available bytes in the buffer
 * \param psOut     Output decoded frame, written only on success
 *
 * \return TRUE when a frame was decoded
 */
bool Shc31GsUsbDecodeRxFrame(const uint8_t* punFrame, uint32_t unAvail, Shc31GsUsbRxFrameStruct* psOut)
{
    bool bDecoded = false;

    if ((punFrame != nullptr) && (psOut != nullptr) && (unAvail >= SHC31_GSUSB_FRAME_HEADER_SIZE))
    {
        uint32_t unFrameFlags = (uint32_t)punFrame[10];
        bool bFdFrame = ((unFrameFlags & SHC31_GSUSB_FRAME_FLAG_FD) != 0U) ? true : false;
        uint32_t unFrameLenWithoutTs = bFdFrame ? SHC31_GSUSB_FRAME_FD_SIZE : SHC31_GSUSB_FRAME_CLASSIC_SIZE;
        uint32_t unFrameLenWithTs = bFdFrame ? SHC31_GSUSB_FRAME_FD_TS_SIZE : SHC31_GSUSB_FRAME_CLASSIC_TS_SIZE;
        bool bTimestamped = (unAvail >= unFrameLenWithTs) ? true : false;

        if (unAvail >= ((bTimestamped == true) ? unFrameLenWithTs : unFrameLenWithoutTs))
        {
            uint32_t unRawId = Shc31GsUsbGetU32(punFrame + 4);
            uint32_t unDlc = (uint32_t)punFrame[8];
            uint32_t unPayloadOffset = SHC31_GSUSB_FRAME_HEADER_SIZE;
            uint32_t unTimestampOffset = unFrameLenWithTs - 4U;

            psOut->EchoId = Shc31GsUsbGetU32(punFrame + 0);
            psOut->unRawCanId = unRawId;
            psOut->bExtended = ((unRawId & SHC31_GSUSB_ID_FLAG_EFF) != 0U) ? true : false;
            psOut->bRtr = ((unRawId & SHC31_GSUSB_ID_FLAG_RTR) != 0U) ? true : false;
            psOut->bFd = bFdFrame;
            psOut->bBrs = ((unFrameFlags & SHC31_GSUSB_FRAME_FLAG_BRS) != 0U) ? true : false;
            psOut->bEsi = ((unFrameFlags & SHC31_GSUSB_FRAME_FLAG_ESI) != 0U) ? true : false;
            psOut->bOverflow = ((unFrameFlags & SHC31_GSUSB_FRAME_FLAG_OVERFLOW) != 0U) ? true : false;
            psOut->MsgId = unRawId & (psOut->bExtended ? SHC31_GSUSB_ID_EFF_MASK : SHC31_GSUSB_ID_SFF_MASK);

            if (bFdFrame == true)
            {
                unDlc = Shc31GsUsbFdDlcToLen(unDlc);
            }

            psOut->unDataLen = (unDlc == SHC31_GSUSB_FD_DLC_INVALID) ? 0U : unDlc;
            psOut->bHasTimestamp = false;
            psOut->unTimestampUs = 0U;

            if ((psOut->unDataLen > 0U) && ((unPayloadOffset + psOut->unDataLen) <= unAvail))
            {
                memcpy(psOut->aunData, punFrame + unPayloadOffset, psOut->unDataLen);
            }
            else
            {
                psOut->unDataLen = 0U;
            }

            if (bTimestamped == true)
            {
                psOut->bHasTimestamp = true;
                psOut->unTimestampUs = Shc31GsUsbGetU32(punFrame + unTimestampOffset);
            }

            bDecoded = true;
        }
    }

    return bDecoded;
}

/**
 * Extends the 32-bit 1 MHz device timestamp across its ~71.6 minute wrap.
 *
 * \param unRawUs           Raw 32-bit timestamp of the current frame
 * \param punLastRawUs      Per-device last raw timestamp, updated in place
 * \param pullWrapOffsetUs  Per-device accumulated wrap offset in microseconds
 *
 * \return Wrap-extended timestamp in microseconds
 */
uint64_t Shc31GsUsbExtendTimestamp(uint32_t unRawUs, uint32_t* punLastRawUs, uint64_t* pullWrapOffsetUs)
{
    uint64_t ullExtended = 0U;

    if ((punLastRawUs != nullptr) && (pullWrapOffsetUs != nullptr))
    {
        const uint32_t HALF_RANGE = 0x80000000U;
        const uint64_t WRAP_PERIOD = 0x100000000ULL;

        if ((*pullWrapOffsetUs == 0U) && (*punLastRawUs == 0U))
        {
            /** First frame on this device: anchor (the caller initialises
                both state words to 0; a genuine first raw of 0 anchors to
                the identical state, so the convention is lossless) */
            *punLastRawUs = unRawUs;
        }
        else if ((unRawUs < *punLastRawUs) && ((*punLastRawUs - unRawUs) > HALF_RANGE))
        {
            /** Backward jump across half the range: one wrap occurred */
            *pullWrapOffsetUs += WRAP_PERIOD;
            *punLastRawUs = unRawUs;
        }
        else if ((unRawUs > *punLastRawUs) && ((unRawUs - *punLastRawUs) > HALF_RANGE))
        {
            /** Forward jump across half the range: the device restarted and
                its counter re-anchored; reset the wrap state so the caller
                sees the jump and re-anchors its QPC conversion */
            *pullWrapOffsetUs = 0U;
            *punLastRawUs = unRawUs;
        }
        else
        {
            *punLastRawUs = unRawUs;
        }

        ullExtended = *pullWrapOffsetUs + (uint64_t)unRawUs;
    }

    return ullExtended;
}

