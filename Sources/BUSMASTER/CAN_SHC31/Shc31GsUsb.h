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
 * \file           Shc31GsUsb.h
 * \brief          Pure gs_usb wire codec for the DSD SH-C31x (gs_usb) driver
 *
 * \section DESCRIPTION
 * Pure, host-testable gs_usb protocol codec: vendor request numbers, mode
 * and frame flags, the gs_host_frame TX encoder and RX decoder, the CAN-FD
 * DLC/byte-length maps and the 1 MHz timestamp wrap-extension helper.
 * Everything here is byte-level wire logic with no I/O, no hardware access
 * and no global mutable state, so it is unit-testable on a host PC and
 * shared unchanged between the WinUSB driver layer (CAN_SHC31.cpp)
 *
 * Protocol facts (from the gs_usb firmware sources: include/gs_usb.h,
 * src/usbd_gs_can.c, src/can.c, DSD SH-C31A-firmware-v1.5.patch):
 *   - vendor requests: HOST_FORMAT=0, BITTIMING=1, MODE=2, BERR=3,
 *     BT_CONST=4, DEVICE_CONFIG=5, TIMESTAMP=6, IDENTIFY=7, GET_USER_ID=8,
 *     SET_USER_ID=9, DATA_BITTIMING=10, BT_CONST_EXT=11, SET_TERMINATION=12,
 *     GET_TERMINATION=13, GET_STATE=14; DSD extras SET_PIN_STATUS=24,
 *     GET_PIN_STATUS=25,
 *   - mode: RESET=0/START=1; flags LISTEN_ONLY=0, LOOP_BACK=1,
 *     TRIPLE_SAMPLE=2, ONE_SHOT=3, HW_TIMESTAMP=4, PAD_PKTS_TO_MAX_PKT_SIZE=7,
 *     FD=8, BERR_REPORTING=12,
 *   - frame flags: OVERFLOW=bit0, FD=bit1, BRS=bit2, ESI=bit3; CAN id flags
 *     EFF=bit31, RTR=bit30, ERR=bit29,
 *   - echo_id = 0xFFFFFFFF marks an RX frame; host TX uses echo_id 0..29
 *     (max 30 frames in flight per device),
 *   - FD DLC coding: 0..8 -> 0..8, 12->9, 16->A, 20->B, 24->C, 32->D,
 *     48->E, 64->F,
 *   - bulk packet size 64 bytes (Full Speed); the driver does not set the
 *     pad flag, so every frame transfer is exactly one gs_host_frame and
 *     ends with a short USB packet,
 *   - the u32 timestamp suffix (microseconds at 1 MHz) is appended when the
 *     HW_TIMESTAMP mode flag is set: classic frame 12+8+4=24 bytes, FD
 *     frame 12+64+4=80 bytes; TX frames are always written at the full
 *     union size of 80 bytes because the firmware arms the bulk OUT
 *     endpoint for sizeof(struct gs_host_frame) = 80 bytes.
 *
 */

#ifndef __INCShc31GsUsb
#define __INCShc31GsUsb

#include <stdint.h>
#include <stddef.h>

/** gs_usb channel (CAN) device interface GUID, string form. The device is
    auto-bound to WinUSB via MS OS descriptors. */
#define SHC31_GSUSB_DEVICE_INTERFACE_GUID_TXT            "{c15b4308-04d3-11e6-b3ea-6057189e6443}"

/** Vendor request: host byte-order announcement, payload u32 0x0000beef */
#define SHC31_GSUSB_BREQ_HOST_FORMAT                     0U

/** Vendor request: write gs_device_bittiming (nominal phase) */
#define SHC31_GSUSB_BREQ_BITTIMING                       1U

/** Vendor request: write gs_device_mode {mode, flags} */
#define SHC31_GSUSB_BREQ_MODE                            2U

/** Vendor request: error counter reporting (unused by this driver) */
#define SHC31_GSUSB_BREQ_BERR                            3U

/** Vendor request: read gs_device_bt_const (classic limits) */
#define SHC31_GSUSB_BREQ_BT_CONST                        4U

/** Vendor request: read gs_device_config (sw_version etc.) */
#define SHC31_GSUSB_BREQ_DEVICE_CONFIG                   5U

/** Vendor request: read the u32 timestamp counter (unused; frames carry it) */
#define SHC31_GSUSB_BREQ_TIMESTAMP                       6U

/** Vendor request: LED identify, gs_identify_mode {u32 1=blink / 0=stop} */
#define SHC31_GSUSB_BREQ_IDENTIFY                        7U

/** Vendor request: read u32 user id (feature bit 6) */
#define SHC31_GSUSB_BREQ_GET_USER_ID                     8U

/** Vendor request: write u32 user id (feature bit 6) */
#define SHC31_GSUSB_BREQ_SET_USER_ID                     9U

/** Vendor request: write gs_device_bittiming (data phase) */
#define SHC31_GSUSB_BREQ_DATA_BITTIMING                  10U

/** Vendor request: read gs_device_bt_const_extended (nominal + data limits
    and feature word); only answered by FD firmware */
#define SHC31_GSUSB_BREQ_BT_CONST_EXT                    11U

/** Vendor request: termination control, unsupported by this device */
#define SHC31_GSUSB_BREQ_SET_TERMINATION                 12U

/** Vendor request: termination state read, unsupported by this device */
#define SHC31_GSUSB_BREQ_GET_TERMINATION                 13U

/** Vendor request: read gs_device_state {state, rxerr, txerr} (v1.5) */
#define SHC31_GSUSB_BREQ_GET_STATE                       14U

/** DSD extra vendor request: set pin status (BOOT0 unlock/lock), OUT 12
    bytes {u16 operation, u16 pin id, 8 reserved} (patch line 711) */
#define SHC31_GSUSB_BREQ_SET_PIN_STATUS                  24U

/** DSD extra vendor request: get pin status, IN 2 bytes, wValue = pin id */
#define SHC31_GSUSB_BREQ_GET_PIN_STATUS                  25U

/** gs_device_mode value: reset the channel (stop the bus) */
#define SHC31_GSUSB_MODE_RESET                           0U

/** gs_device_mode value: start the channel (go on bus) */
#define SHC31_GSUSB_MODE_START                           1U

/** gs_device_mode flag bit 0: listen-only (passive) */
#define SHC31_GSUSB_MODE_FLAG_LISTEN_ONLY                0x00000001U

/** gs_device_mode flag bit 1: loop-back */
#define SHC31_GSUSB_MODE_FLAG_LOOP_BACK                  0x00000002U

/** gs_device_mode flag bit 2: triple sampling */
#define SHC31_GSUSB_MODE_FLAG_TRIPLE_SAMPLE              0x00000004U

/** gs_device_mode flag bit 3: one-shot (no retransmission) */
#define SHC31_GSUSB_MODE_FLAG_ONE_SHOT                   0x00000008U

/** gs_device_mode flag bit 4: append the u32 microsecond timestamp to every
    frame (advertised by DSD v1.5, GS_CAN_FEATURE_HW_TIMESTAMP) */
#define SHC31_GSUSB_MODE_FLAG_HW_TIMESTAMP               0x00000010U

/** gs_device_mode flag bit 7: pad every frame transfer to the 64-byte bulk
    packet size (the device then transfers whole 128-byte buffers) */
#define SHC31_GSUSB_MODE_FLAG_PAD_PKTS                   0x00000080U

/** gs_device_mode flag bit 8: switch the channel to CAN-FD mode */
#define SHC31_GSUSB_MODE_FLAG_FD                         0x00000100U

/** gs_device_mode flag bit 12: bus error reporting */
#define SHC31_GSUSB_MODE_FLAG_BERR_REPORTING             0x00001000U

/** gs_host_frame flags bit 0: receive buffer overflow */
#define SHC31_GSUSB_FRAME_FLAG_OVERFLOW                  0x00000001U

/** gs_host_frame flags bit 1: CAN-FD frame */
#define SHC31_GSUSB_FRAME_FLAG_FD                        0x00000002U

/** gs_host_frame flags bit 2: bit rate switch (FD only) */
#define SHC31_GSUSB_FRAME_FLAG_BRS                       0x00000004U

/** gs_host_frame flags bit 3: error state indicator (FD only) */
#define SHC31_GSUSB_FRAME_FLAG_ESI                       0x00000008U

/** gs_host_frame can_id bit 31: extended (29-bit) identifier */
#define SHC31_GSUSB_ID_FLAG_EFF                          0x80000000U

/** gs_host_frame can_id bit 30: remote transmission request */
#define SHC31_GSUSB_ID_FLAG_RTR                          0x40000000U

/** gs_host_frame can_id bit 29: error message frame (pushed by the firmware
    on controller state transitions and LEC errors; never sent by this
    driver) */
#define SHC31_GSUSB_ID_FLAG_ERR                          0x20000000U

/** gs_host_frame can_id error-class bits of a firmware error frame
    (can.c can_parse_error_status, kernel can/error.h values) */
#define SHC31_GSUSB_ID_ERR_CLASS_LOSTARB                 0x00000002U

/** controller error class (passive/warning/active transitions) */
#define SHC31_GSUSB_ID_ERR_CLASS_CRTL                    0x00000004U

/** protocol error class (LEC errors) */
#define SHC31_GSUSB_ID_ERR_CLASS_PROT                    0x00000008U

/** acknowledgement error class */
#define SHC31_GSUSB_ID_ERR_CLASS_ACK                     0x00000020U

/** bus-off transition class */
#define SHC31_GSUSB_ID_ERR_CLASS_BUSOFF                  0x00000040U

/** gs_host_frame data[2] protocol error types (kernel can/error.h) */
#define SHC31_GSUSB_DATA_PROT_FORM                       0x02U

/** stuff error */
#define SHC31_GSUSB_DATA_PROT_STUFF                      0x04U

/** dominant bit error */
#define SHC31_GSUSB_DATA_PROT_BIT0                       0x08U

/** recessive bit error */
#define SHC31_GSUSB_DATA_PROT_BIT1                       0x10U

/** 29-bit identifier mask */
#define SHC31_GSUSB_ID_EFF_MASK                          0x1FFFFFFFU

/** 11-bit identifier mask */
#define SHC31_GSUSB_ID_SFF_MASK                          0x000007FFU

/** gs_host_frame echo_id value marking a received (not echoed) frame */
#define SHC31_GSUSB_ECHO_ID_RX                           0xFFFFFFFFU

/** Maximum TX frames in flight per device: echo_id pool 0..29 (kernel
    gs_usb.c constraint) */
#define SHC31_GSUSB_ECHO_POOL_MAX                        30U

/** gs_host_frame header size in bytes: 2 u32 + 4 u8 = 12 (struct is
    __packed; the wire header is NOT 16 bytes) */
#define SHC31_GSUSB_FRAME_HEADER_SIZE                    12U

/** Classic frame size without the timestamp suffix: 12 (header) + 8 (data) */
#define SHC31_GSUSB_FRAME_CLASSIC_SIZE                   20U

/** Classic frame size with the u32 timestamp suffix: 12 + 8 + 4
    (union member classic_can_ts = data[8] + timestamp_us) */
#define SHC31_GSUSB_FRAME_CLASSIC_TS_SIZE                24U

/** CAN-FD frame size without the timestamp suffix: 12 (header) + 64 (data) */
#define SHC31_GSUSB_FRAME_FD_SIZE                        76U

/** CAN-FD frame size with the u32 timestamp suffix: 12 + 64 + 4 (union
    member canfd_ts = data[64] + timestamp_us) */
#define SHC31_GSUSB_FRAME_FD_TS_SIZE                     80U

/** Maximum gs_host_frame size (FD frame with timestamp union member =
    sizeof(struct gs_host_frame) on the device); the firmware arms the bulk
    OUT endpoint for exactly this size, so TX frames are always written at
    full length (80) and never over it */
#define SHC31_GSUSB_FRAME_MAX_SIZE                       80U

/** Bulk transfer size for RX reads: larger than the biggest event
    (80-byte FD frame with timestamp); without the pad flag every transfer
    completes at the event's own short-packet boundary */
#define SHC31_GSUSB_RX_XFER_SIZE                         128U

/** Timeout for the pre-session RX drain (one empty-pipe read) */
#define SHC31_RX_DRAIN_TIMEOUT_MS                        20U

/** Upper bound on transfers discarded before the RX thread starts */
#define SHC31_RX_DRAIN_MAX_FRAMES                        32U

/** Bulk packet size (Full Speed wMaxPacketSize, CAN_DATA_MAX_PACKET_SIZE) */
#define SHC31_GSUSB_BULK_PACKET_SIZE                     64U

/** gs_device_bittiming wire size: 5 u32 fields */
#define SHC31_GSUSB_BITTIMING_SIZE                       20U

/** gs_device_mode wire size: 2 u32 fields */
#define SHC31_GSUSB_MODE_SIZE                            8U

/** gs_identify_mode wire size: 1 u32 field */
#define SHC31_GSUSB_IDENTIFY_SIZE                        4U

/** gs_device_bt_const_extended wire size: 18 u32 fields. The device STALLs
    a short request (measured 2026-11-03: 68 bytes -> ERROR_GEN_FAILURE),
    so the host must ask for the full 72 bytes. */
#define SHC31_GSUSB_BT_CONST_EXT_SIZE                    72U

/** gs_device_bt_const wire size: 10 u32 fields */
#define SHC31_GSUSB_BT_CONST_SIZE                        40U

/** gs_device_config wire size: 12 bytes (u8 rsvd[3], u8 icount, u32
    sw_version, u32 hw_version); sw_version is at offset 4, not 8 */
#define SHC31_GSUSB_DEVICE_CONFIG_SIZE                   12U

/** Offset of sw_version in the gs_device_config answer */
#define SHC31_GSUSB_DEVICE_CONFIG_SW_VERSION_OFFSET      4U

/** Feature word of the DSD v1.4 firmware (FD, IDENTIFY, USER_ID,
    HW_TIMESTAMP, BT_CONST_EXT, LISTEN_ONLY, LOOP_BACK, ONE_SHOT,
    PAD_PKTS) */
#define SHC31_GSUSB_FEATURE_WORD_DSD_V14                 0x000005FBU

/** Feature word of the DSD v1.5 firmware (v1.4 plus GET_STATE, bit 13) */
#define SHC31_GSUSB_FEATURE_WORD_DSD_V15                 0x000025FBU

/** Feature word of legacy stock canable2 firmware (no FD, no identify) */
#define SHC31_GSUSB_FEATURE_WORD_LEGACY_STOCK            0x000000F3U

/** gs_usb feature bit 5: IDENTIFY supported */
#define SHC31_GSUSB_FEATURE_IDENTIFY                     0x00000020U

/** gs_usb feature bit 6: USER ID supported */
#define SHC31_GSUSB_FEATURE_USER_ID                      0x00000040U

/** gs_usb feature bit 8: CAN-FD supported */
#define SHC31_GSUSB_FEATURE_FD                           0x00000100U

/** gs_usb feature bit 10: BT_CONST_EXT answered */
#define SHC31_GSUSB_FEATURE_BT_CONST_EXT                 0x00000400U

/** Value of the HOST_FORMAT payload (candleLight ignores the byte order) */
#define SHC31_GSUSB_HOST_FORMAT_MAGIC                    0x0000BEEFU

/** Return value of the DLC mapping helpers for an invalid input */
#define SHC31_GSUSB_FD_DLC_INVALID                       0xFFU

/**
 * Decoded gs_host_frame content, BusMaster-neutral (no BusMaster headers
 * are pulled in, keeping the module host-testable).
 */
typedef struct SHC31_GSUSB_RX_FRAME_STRUCT
{
    uint32_t EchoId;                             /** echo_id: 0xFFFFFFFF for RX, 0..29 for TX echoes */
    uint32_t unRawCanId;                         /** raw can_id including the EFF/RTR/ERR flag bits */
    uint32_t MsgId;                              /** 11- or 29-bit CAN identifier (flags stripped) */
    bool bExtended;                              /** CAN id bit 31 was set (29-bit identifier) */
    bool bRtr;                                   /** CAN id bit 30 was set (remote frame; classic only) */
    bool bFd;                                    /** frame flags bit 1 (CAN-FD) */
    bool bBrs;                                   /** frame flags bit 2 (bit rate switch) */
    bool bEsi;                                   /** frame flags bit 3 (error state indicator) */
    bool bOverflow;                              /** frame flags bit 0 (device buffer overflow) */
    bool bHasTimestamp;                          /** the u32 timestamp suffix was present and captured */
    uint32_t unTimestampUs;                      /** raw 1 MHz device timestamp (when bHasTimestamp) */
    uint32_t unDataLen;                          /** data length in bytes 0..64 (FD DLC-decoded) */
    uint8_t aunData[64];                         /** frame payload */
} Shc31GsUsbRxFrameStruct;

/**
 * Stores a little-endian u32 into a byte buffer (wire order).
 *
 * \param punBuf   Output buffer, at least 4 bytes
 * \param unValue  Value to store
 *
 * \return None
 */
void Shc31GsUsbPutU32(uint8_t* punBuf, uint32_t unValue);

/**
 * Reads a little-endian u32 from a byte buffer (wire order).
 *
 * \param punBuf  Input buffer, at least 4 bytes
 *
 * \return The read value
 */
uint32_t Shc31GsUsbGetU32(const uint8_t* punBuf);

/**
 * Converts a CAN FD data length in bytes (0..8, 12, 16, 20, 24, 32, 48, 64)
 * to the FD DLC code used in gs_host_frame::can_dlc.
 *
 * \param unDataLen  Data length in bytes
 *
 * \return DLC code 0..15, or SHC31_GSUSB_FD_DLC_INVALID when the length is
 *         not expressible
 */
uint32_t Shc31GsUsbFdLenToDlc(uint32_t unDataLen);

/**
 * Converts an FD DLC code from gs_host_frame::can_dlc to the data length in
 * bytes. This is the inverse of Shc31GsUsbFdLenToDlc.
 *
 * \param unDlcCode  DLC code 0..15
 *
 * \return Data length in bytes 0..64, or SHC31_GSUSB_FD_DLC_INVALID
 */
uint32_t Shc31GsUsbFdDlcToLen(uint32_t unDlcCode);

/**
 * Encodes one TX frame into the gs_host_frame wire format (little-endian
 * u32 fields, 4-byte aligned packing). The frame includes the u32
 * timestamp union member position (zero-filled) so the layout is valid for
 * both classic and FD writes.
 *
 * \param unMsgId      CAN identifier (11- or 29-bit, masked to its width)
 * \param bExtended    TRUE for a 29-bit identifier
 * \param bRtr         TRUE for a remote frame (classic only, no payload)
 * \param bFd          TRUE for a CAN-FD frame
 * \param bBrs         TRUE to set the BRS flag on an FD frame
 * \param unDataLen    Payload length in bytes (0..8 classic, FD DLC lengths)
 * \param punData      Payload bytes (may be null when unDataLen is 0 or RTR)
 * \param unEchoId     Echo pool slot for the TX (0..29)
 * \param punFrame     Output frame buffer, at least SHC31_GSUSB_FRAME_MAX_SIZE
 * \param unFrameSize  Output buffer size in bytes
 *
 * \return Encoded frame length in bytes (always
 *         SHC31_GSUSB_FRAME_MAX_SIZE = 80, the firmware's bulk OUT transfer
 *         size, with a zero-filled tail), or 0 when the parameters do not
 *         encode
 */
uint32_t Shc31GsUsbEncodeTxFrame(uint32_t unMsgId, bool bExtended, bool bRtr, bool bFd, bool bBrs, uint32_t unDataLen, const uint8_t* punData, uint32_t unEchoId, uint8_t* punFrame, uint32_t unFrameSize);

/**
 * Decodes one gs_host_frame from a bulk RX buffer. The frame length is
 * derived from the FD flag (classic 20/24 bytes, FD 76/80 bytes); the
 * timestamp suffix is taken when the buffer is long enough, else
 * bHasTimestamp stays FALSE.
 *
 * \param punFrame  Input buffer (bulk transfer content)
 * \param unAvail   Available bytes in the buffer
 * \param psOut     Output decoded frame, written only on success
 *
 * \return TRUE when a frame was decoded; FALSE for null pointers or a
 *         buffer shorter than the frame header
 */
bool Shc31GsUsbDecodeRxFrame(const uint8_t* punFrame, uint32_t unAvail, Shc31GsUsbRxFrameStruct* psOut);

/**
 * Extends the 32-bit 1 MHz device timestamp across its ~71.6 minute wrap.
 * The per-device state is updated in place: a backward jump across half of
 * the u32 range counts one wrap (2^32 microseconds); a forward jump across
 * half of the range is treated as a device restart and re-anchors the
 * state (the extended value jumps back, which the caller detects and uses
 * to re-anchor its QPC conversion).
 *
 * \param unRawUs           Raw 32-bit timestamp of the current frame
 * \param punLastRawUs      Per-device last raw timestamp, updated in place
 * \param pullWrapOffsetUs  Per-device accumulated wrap offset in
 *                          microseconds, updated in place
 *
 * \return Wrap-extended timestamp in microseconds
 */
uint64_t Shc31GsUsbExtendTimestamp(uint32_t unRawUs, uint32_t* punLastRawUs, uint64_t* pullWrapOffsetUs);

#endif
