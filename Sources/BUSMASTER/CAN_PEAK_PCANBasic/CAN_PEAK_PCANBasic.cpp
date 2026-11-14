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
 * \file      CAN_PEAK_PCANBasic.cpp
 * \brief     BUSMASTER-NG driver for PEAK-System CAN interfaces (USB, PCI, PC Card, LAN) through the PCAN-Basic API
 *
 * PCANBasic.dll comes with the PEAK device driver package and is loaded at run time. One PCAN-Basic channel is one
 * BUSMASTER channel. Received messages carry the time stamp of the interface; transmitted messages are put in the
 * buffers when the interface has accepted them, with the time of the PC.
 */

#include "CAN_PEAK_PCANBasic_stdafx.h"
#include "CAN_PEAK_PCANBasic.h"
#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "PCANBasicDyn.h"

#define USAGE_EXPORT
#include "CAN_PEAK_PCANBasic_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_PEAK_PCANBasic, CWinApp)
END_MESSAGE_MAP()

CCAN_PEAK_PCANBasic::CCAN_PEAK_PCANBasic()
{
}

CCAN_PEAK_PCANBasic theApp;

/* Protects the client list and the client buffers: written by the read thread and by the sending thread */
static CRITICAL_SECTION sg_csClients;

BOOL CCAN_PEAK_PCANBasic::InitInstance()
{
    CWinApp::InitInstance();
    InitializeCriticalSection(&sg_csClients);
    return TRUE;
}

/* ------------------------------------------------------------------------------------------------------------------
 * PCAN-Basic library
 * ------------------------------------------------------------------------------------------------------------------ */

static HMODULE sg_hPCANBasic = nullptr;
static PFN_PCAN_INITIALIZE   pfPCAN_Initialize = nullptr;
static PFN_PCAN_UNINITIALIZE pfPCAN_Uninitialize = nullptr;
static PFN_PCAN_READ         pfPCAN_Read = nullptr;
static PFN_PCAN_WRITE        pfPCAN_Write = nullptr;
static PFN_PCAN_GETVALUE     pfPCAN_GetValue = nullptr;
static PFN_PCAN_SETVALUE     pfPCAN_SetValue = nullptr;
static PFN_PCAN_GETERRORTEXT pfPCAN_GetErrorText = nullptr;

static std::string sg_strLastError;
static HWND sg_hOwnerWnd = nullptr;

static void vUnloadPCANBasic()
{
    if (sg_hPCANBasic != nullptr)
    {
        FreeLibrary(sg_hPCANBasic);
        sg_hPCANBasic = nullptr;
    }
    pfPCAN_Initialize = nullptr;
    pfPCAN_Uninitialize = nullptr;
    pfPCAN_Read = nullptr;
    pfPCAN_Write = nullptr;
    pfPCAN_GetValue = nullptr;
    pfPCAN_SetValue = nullptr;
    pfPCAN_GetErrorText = nullptr;
}

static bool bLoadPCANBasic()
{
    if (sg_hPCANBasic != nullptr)
    {
        return true;
    }
    sg_hPCANBasic = LoadLibrary("PCANBasic.dll");
    if (sg_hPCANBasic == nullptr)
    {
        sg_strLastError = "PCANBasic.dll was not found. It is installed with the PEAK-System device driver "
                          "(www.peak-system.com).";
        return false;
    }
    pfPCAN_Initialize   = (PFN_PCAN_INITIALIZE)   GetProcAddress(sg_hPCANBasic, "CAN_Initialize");
    pfPCAN_Uninitialize = (PFN_PCAN_UNINITIALIZE) GetProcAddress(sg_hPCANBasic, "CAN_Uninitialize");
    pfPCAN_Read         = (PFN_PCAN_READ)         GetProcAddress(sg_hPCANBasic, "CAN_Read");
    pfPCAN_Write        = (PFN_PCAN_WRITE)        GetProcAddress(sg_hPCANBasic, "CAN_Write");
    pfPCAN_GetValue     = (PFN_PCAN_GETVALUE)     GetProcAddress(sg_hPCANBasic, "CAN_GetValue");
    pfPCAN_SetValue     = (PFN_PCAN_SETVALUE)     GetProcAddress(sg_hPCANBasic, "CAN_SetValue");
    pfPCAN_GetErrorText = (PFN_PCAN_GETERRORTEXT) GetProcAddress(sg_hPCANBasic, "CAN_GetErrorText");
    if (!pfPCAN_Initialize || !pfPCAN_Uninitialize || !pfPCAN_Read || !pfPCAN_Write || !pfPCAN_GetValue ||
        !pfPCAN_SetValue || !pfPCAN_GetErrorText)
    {
        vUnloadPCANBasic();
        sg_strLastError = "PCANBasic.dll is too old or damaged: install the current PEAK-System device driver.";
        return false;
    }
    return true;
}

/* Text of a PCAN-Basic status code, in English */
static std::string strStatusText(TPCANStatus nStatus)
{
    char acText[MAX_LENGTH_VERSION_STRING] = "";
    if ((pfPCAN_GetErrorText == nullptr) || (pfPCAN_GetErrorText(nStatus, 0x09, acText) != PCAN_ERROR_OK))
    {
        sprintf_s(acText, "PCAN-Basic error 0x%X", (unsigned int)nStatus);
    }
    return acText;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Time: everything in units of 0.1 ms of the performance counter, like the other drivers
 * ------------------------------------------------------------------------------------------------------------------ */

static LONGLONG llNow01ms()
{
    LARGE_INTEGER sCount, sFrequency;
    QueryPerformanceCounter(&sCount);
    QueryPerformanceFrequency(&sFrequency);
    return (sCount.QuadPart / sFrequency.QuadPart) * 10000 +
           ((sCount.QuadPart % sFrequency.QuadPart) * 10000) / sFrequency.QuadPart;
}

static SYSTEMTIME sg_CurrSysTime;
static UINT64 sg_TimeStamp = 0;
static LARGE_INTEGER sg_QueryTickCount;

/* ------------------------------------------------------------------------------------------------------------------
 * Hardware and channels
 * ------------------------------------------------------------------------------------------------------------------ */

/* One PCAN-Basic channel found on this PC */
struct SPcanHardware
{
    TPCANHandle m_hChannel;
    DWORD       m_dwDeviceId;
    DWORD       m_dwCondition;
    DWORD       m_dwFeatures;
    std::string m_strDescription;   // also the name saved in the configuration
    std::string m_strFirmware;
};

/* One BUSMASTER channel */
struct SPcanChannel
{
    TPCANHandle m_hChannel;
    std::string m_strDescription;
    bool        m_bInitialized;
    HANDLE      m_hRxEvent;
    bool        m_bTimeOffsetSet;
    LONGLONG    m_llTimeOffset;     // performance counter time - interface time, in 0.1 ms
    UCHAR       m_ucTxErrCount;
    UCHAR       m_ucRxErrCount;
    UCHAR       m_ucPeakTxErrCount;
    UCHAR       m_ucPeakRxErrCount;
};

static SPcanChannel sg_asChannels[CHANNEL_ALLOWED];
static UINT sg_nNoOfChannels = 0;
static SCONTROLLER_DETAILS sg_asControllerDetails[defNO_OF_CHANNELS];

enum
{
    STATE_DRIVER_SELECTED = 0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};
static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;

/* Name of a channel handle, as PEAK writes it ("USB 1", "PCI 2", ...) */
static std::string strChannelName(TPCANHandle hChannel)
{
    char acName[32] = "";
    if (hChannel >= 0x51 && hChannel <= 0x58)        sprintf_s(acName, "USB %d", hChannel - 0x51 + 1);
    else if (hChannel >= 0x509 && hChannel <= 0x510) sprintf_s(acName, "USB %d", hChannel - 0x509 + 9);
    else if (hChannel >= 0x41 && hChannel <= 0x48)   sprintf_s(acName, "PCI %d", hChannel - 0x41 + 1);
    else if (hChannel >= 0x409 && hChannel <= 0x410) sprintf_s(acName, "PCI %d", hChannel - 0x409 + 9);
    else if (hChannel >= 0x801 && hChannel <= 0x810) sprintf_s(acName, "LAN %d", hChannel - 0x801 + 1);
    else if (hChannel == PCAN_PCCBUS1 || hChannel == PCAN_PCCBUS2)
    {
        sprintf_s(acName, "PC Card %d", hChannel - PCAN_PCCBUS1 + 1);
    }
    else                                             sprintf_s(acName, "0x%X", hChannel);
    return acName;
}

/* All the channel handles of PCAN-Basic for plug and play interfaces */
static std::vector<TPCANHandle> vecAllHandles()
{
    std::vector<TPCANHandle> vecHandles;
    for (TPCANHandle h = 0x51; h <= 0x58; h++)   vecHandles.push_back(h);
    for (TPCANHandle h = 0x509; h <= 0x510; h++) vecHandles.push_back(h);
    for (TPCANHandle h = 0x41; h <= 0x48; h++)   vecHandles.push_back(h);
    for (TPCANHandle h = 0x409; h <= 0x410; h++) vecHandles.push_back(h);
    for (TPCANHandle h = 0x801; h <= 0x810; h++) vecHandles.push_back(h);
    vecHandles.push_back(PCAN_PCCBUS1);
    vecHandles.push_back(PCAN_PCCBUS2);
    return vecHandles;
}

/* Number of the channel within its kind (USB 2 -> 2), shown as "Driver ID" in the hardware selection */
static DWORD dwChannelNumber(TPCANHandle hChannel)
{
    std::string strName = strChannelName(hChannel);
    size_t nSpace = strName.rfind(' ');
    return (nSpace == std::string::npos) ? 0 : (DWORD)atol(strName.c_str() + nSpace + 1);
}

/* Name, description and firmware of a channel found */
static void vCompleteHardware(SPcanHardware& sHw, const char* pacHwName)
{
    // the device ID is only written when it was set (PCAN-View, "Identify"): 255 and FFFFFFFF are the defaults
    char acDescription[128];
    if ((sHw.m_dwDeviceId != 0xFF) && (sHw.m_dwDeviceId != 0xFFFFFFFF))
    {
        sprintf_s(acDescription, "PEAK %s (%s), device ID %lu", pacHwName, strChannelName(sHw.m_hChannel).c_str(),
                  sHw.m_dwDeviceId);
    }
    else
    {
        sprintf_s(acDescription, "PEAK %s (%s)", pacHwName, strChannelName(sHw.m_hChannel).c_str());
    }
    sHw.m_strDescription = acDescription;

    // asked to the device once per channel and description: the list is read several times while a driver is chosen
    static std::map<std::string, std::string> s_mapFirmware;
    std::map<std::string, std::string>::iterator itFirmware = s_mapFirmware.find(sHw.m_strDescription);
    if (itFirmware != s_mapFirmware.end())
    {
        sHw.m_strFirmware = itFirmware->second;
    }
    else if (sHw.m_dwCondition == PCAN_CHANNEL_AVAILABLE)
    {
        // the firmware is only given by an open channel: open it for a moment in listen-only, so that nothing is sent
        // on the bus (about 0.4 s with a PCAN-USB, once per program start)
        DWORD dwOn = PCAN_PARAMETER_ON, dwOff = PCAN_PARAMETER_OFF;
        if ((pfPCAN_SetValue(sHw.m_hChannel, PCAN_LISTEN_ONLY, &dwOn, sizeof(dwOn)) == PCAN_ERROR_OK) &&
            (pfPCAN_Initialize(sHw.m_hChannel, PCAN_BAUD_500K, 0, 0, 0) == PCAN_ERROR_OK))
        {
            char acFirmware[MAX_LENGTH_VERSION_STRING] = "";
            if (pfPCAN_GetValue(sHw.m_hChannel, PCAN_FIRMWARE_VERSION, acFirmware, sizeof(acFirmware)) == PCAN_ERROR_OK)
            {
                sHw.m_strFirmware = acFirmware;
            }
            pfPCAN_Uninitialize(sHw.m_hChannel);
        }
        pfPCAN_SetValue(sHw.m_hChannel, PCAN_LISTEN_ONLY, &dwOff, sizeof(dwOff));
        s_mapFirmware[sHw.m_strDescription] = sHw.m_strFirmware;   // also when unknown: not asked again
    }
    if (sHw.m_dwCondition == PCAN_CHANNEL_OCCUPIED)
    {
        sHw.m_strFirmware += sHw.m_strFirmware.empty() ? "in use" : " (in use)";
    }
}

/* Lists the PEAK channels present on this PC. PCAN-Basic 4.2 and later give them all with one call; older versions
   are asked channel by channel, which is slow because of the LAN channels. */
static std::vector<SPcanHardware> vecScanHardware()
{
    std::vector<SPcanHardware> vecFound;
    if (pfPCAN_GetValue == nullptr)
    {
        return vecFound;
    }

    DWORD dwAttached = 0;
    if (pfPCAN_GetValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS_COUNT, &dwAttached, sizeof(dwAttached)) == PCAN_ERROR_OK)
    {
        if (dwAttached > 0)
        {
            std::vector<TPCANChannelInformation> vecInfo(dwAttached);
            if (pfPCAN_GetValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS, &vecInfo[0],
                                (DWORD)(vecInfo.size() * sizeof(TPCANChannelInformation))) == PCAN_ERROR_OK)
            {
                for (size_t i = 0; (i < vecInfo.size()) && (vecFound.size() < defNO_OF_CHANNELS); i++)
                {
                    if (vecInfo[i].channel_condition == PCAN_CHANNEL_UNAVAILABLE)
                    {
                        continue;
                    }
                    SPcanHardware sHw;
                    sHw.m_hChannel = vecInfo[i].channel_handle;
                    sHw.m_dwDeviceId = vecInfo[i].device_id;
                    sHw.m_dwCondition = vecInfo[i].channel_condition;
                    sHw.m_dwFeatures = vecInfo[i].device_features;
                    char acHwName[MAX_LENGTH_HARDWARE_NAME + 1] = "";
                    memcpy(acHwName, vecInfo[i].device_name, MAX_LENGTH_HARDWARE_NAME);
                    vCompleteHardware(sHw, acHwName[0] ? acHwName : "PCAN");
                    vecFound.push_back(sHw);
                }
            }
        }
        return vecFound;
    }

    std::vector<TPCANHandle> vecHandles = vecAllHandles();
    for (size_t i = 0; (i < vecHandles.size()) && (vecFound.size() < defNO_OF_CHANNELS); i++)
    {
        SPcanHardware sHw;
        sHw.m_hChannel = vecHandles[i];
        sHw.m_dwCondition = PCAN_CHANNEL_UNAVAILABLE;
        sHw.m_dwFeatures = 0;
        if ((pfPCAN_GetValue(sHw.m_hChannel, PCAN_CHANNEL_CONDITION, &sHw.m_dwCondition,
                             sizeof(sHw.m_dwCondition)) != PCAN_ERROR_OK) ||
            (sHw.m_dwCondition == PCAN_CHANNEL_UNAVAILABLE))
        {
            continue;
        }
        char acHwName[MAX_LENGTH_HARDWARE_NAME + 1] = "";
        if (pfPCAN_GetValue(sHw.m_hChannel, PCAN_HARDWARE_NAME, acHwName, MAX_LENGTH_HARDWARE_NAME) != PCAN_ERROR_OK)
        {
            strcpy_s(acHwName, "PCAN");
        }
        sHw.m_dwDeviceId = 0xFFFFFFFF;
        pfPCAN_GetValue(sHw.m_hChannel, PCAN_DEVICE_ID, &sHw.m_dwDeviceId, sizeof(sHw.m_dwDeviceId));
        vCompleteHardware(sHw, acHwName);
        vecFound.push_back(sHw);
    }
    return vecFound;
}

static int nFindHardware(const std::vector<SPcanHardware>& vecHardware, const std::string& strDescription)
{
    for (size_t i = 0; i < vecHardware.size(); i++)
    {
        if (vecHardware[i].m_strDescription == strDescription)
        {
            return (int)i;
        }
    }
    return -1;
}

static void vSetChannels(const std::vector<SPcanHardware>& vecHardware, const int* pnSelection, int nCount)
{
    sg_nNoOfChannels = 0;
    for (int i = 0; (i < nCount) && (i < (int)CHANNEL_ALLOWED); i++)
    {
        SPcanChannel& sChannel = sg_asChannels[i];
        sChannel.m_hChannel = vecHardware[pnSelection[i]].m_hChannel;
        sChannel.m_strDescription = vecHardware[pnSelection[i]].m_strDescription;
        sChannel.m_bInitialized = false;
        sChannel.m_hRxEvent = nullptr;
        sg_nNoOfChannels++;
    }
}

/* Bit rate for CAN_Initialize: the PEAK value of a standard speed, otherwise the BTR0/BTR1 of the configuration
   (calculated for 16 MHz, the clock of the PEAK BTR values) */
static TPCANBaudrate wGetBaudrate(const SCONTROLLER_DETAILS& sDetails)
{
    static const struct { long m_lBaudRate; TPCANBaudrate m_wPeak; } s_asRates[] =
    {
        {1000000, PCAN_BAUD_1M}, {800000, PCAN_BAUD_800K}, {500000, PCAN_BAUD_500K}, {250000, PCAN_BAUD_250K},
        {125000, PCAN_BAUD_125K}, {100000, PCAN_BAUD_100K}, {95238, PCAN_BAUD_95K}, {83333, PCAN_BAUD_83K},
        {50000, PCAN_BAUD_50K}, {47619, PCAN_BAUD_47K}, {33333, PCAN_BAUD_33K}, {20000, PCAN_BAUD_20K},
        {10000, PCAN_BAUD_10K}, {5000, PCAN_BAUD_5K}
    };
    long lBaudRate = atol(sDetails.m_omStrBaudrate.c_str());
    for (size_t i = 0; i < sizeof(s_asRates) / sizeof(s_asRates[0]); i++)
    {
        long lDiff = lBaudRate - s_asRates[i].m_lBaudRate;
        if (lDiff < 0)
        {
            lDiff = -lDiff;
        }
        if (lDiff * 100 <= s_asRates[i].m_lBaudRate)    // within 1 %: 83000 or 83333 are both 83.3 kbit/s
        {
            return s_asRates[i].m_wPeak;
        }
    }
    if ((sDetails.m_nBTR0BTR1 > 0) && (sDetails.m_nBTR0BTR1 <= 0xFFFF))
    {
        return (TPCANBaudrate)sDetails.m_nBTR0BTR1;
    }
    return PCAN_BAUD_500K;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Clients and their buffers
 * ------------------------------------------------------------------------------------------------------------------ */

#define MAX_BUFF_ALLOWED   16
#define MAX_CLIENT_ALLOWED 16

struct SClientBufMap
{
    DWORD m_dwClientID;
    CBaseCANBufFSE* m_apClientBuf[MAX_BUFF_ALLOWED];
    char m_acClientName[MAX_PATH];
    UINT m_unBufCount;
    SClientBufMap()
    {
        m_dwClientID = 0;
        m_unBufCount = 0;
        memset(m_acClientName, 0, sizeof(m_acClientName));
        memset(m_apClientBuf, 0, sizeof(m_apClientBuf));
    }
};
static SClientBufMap sg_asClients[MAX_CLIENT_ALLOWED];
static UINT sg_unClientCnt = 0;

static bool bGetClientIndex(DWORD dwClientID, UINT& unIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClients[i].m_dwClientID == dwClientID)
        {
            unIndex = i;
            return true;
        }
    }
    return false;
}

static bool bGetClientIndexByName(const char* pacName, UINT& unIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (strcmp(sg_asClients[i].m_acClientName, pacName) == 0)
        {
            unIndex = i;
            return true;
        }
    }
    return false;
}

static DWORD dwGetFreeClientID()
{
    DWORD dwID = 2;     // 1 is the monitor node
    UINT unIndex;
    while (bGetClientIndex(dwID, unIndex))
    {
        dwID++;
    }
    return dwID;
}

static bool bRemoveClient(DWORD dwClientID)
{
    UINT unIndex;
    if (!bGetClientIndex(dwClientID, unIndex))
    {
        return false;
    }
    if (unIndex + 1 < sg_unClientCnt)
    {
        sg_asClients[unIndex] = sg_asClients[sg_unClientCnt - 1];
    }
    sg_asClients[sg_unClientCnt - 1] = SClientBufMap();
    sg_unClientCnt--;
    return true;
}

/* Received message or error: to every client. Transmitted message: Tx for the sender, Rx for the others. */
static void vWriteIntoClientsBuffer(STCANDATA& sCanData, DWORD dwSenderClientID = 0)
{
    EnterCriticalSection(&sg_csClients);
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        STCANDATA sCopy = sCanData;
        if (IS_TX_MESSAGE(sCanData.m_ucDataType) && (sg_asClients[i].m_dwClientID != dwSenderClientID))
        {
            sCopy.m_ucDataType = RX_FLAG;
        }
        for (UINT j = 0; j < sg_asClients[i].m_unBufCount; j++)
        {
            sg_asClients[i].m_apClientBuf[j]->WriteIntoBuffer(&sCopy);
        }
    }
    LeaveCriticalSection(&sg_csClients);
}

/* ------------------------------------------------------------------------------------------------------------------
 * Reading
 * ------------------------------------------------------------------------------------------------------------------ */

static HANDLE sg_hReadThread = nullptr;
static HANDLE sg_hStopEvent = nullptr;

/* Error code capture register of an SJA1000 (bits 7-6 type, bit 5 direction 1 = receiving, bits 4-0 position), as
   BUSMASTER expects it in m_ucReg_ErrCap */
static UCHAR ucGetErrorCode(BYTE byECC)
{
    bool bRx = (byECC & 0x20) != 0;
    if ((byECC & 0x1F) == 0x08)     // CRC sequence
    {
        return bRx ? CRC_ERROR_RX : CRC_ERROR_TX;
    }
    switch (byECC & 0xC0)
    {
        case 0x00: return bRx ? BIT_ERROR_RX : BIT_ERROR_TX;
        case 0x40: return bRx ? FORM_ERROR_RX : FORM_ERROR_TX;
        case 0x80: return bRx ? STUFF_ERROR_RX : STUFF_ERROR_TX;
        default:   return bRx ? OTHER_ERROR_RX : OTHER_ERROR_TX;
    }
}

static void vProcessMessage(UINT unChannel, const TPCANMsg& sMsg, const TPCANTimestamp& sTime)
{
    SPcanChannel& sChannel = sg_asChannels[unChannel];

    // interface time in 0.1 ms, moved onto the performance counter time with the offset seen at the first message
    UINT64 u64Micros = (UINT64)sTime.micros + 1000ULL * ((UINT64)sTime.millis + 0x100000000ULL * sTime.millis_overflow);
    LONGLONG llInterface01ms = (LONGLONG)(u64Micros / 100);
    if (!sChannel.m_bTimeOffsetSet)
    {
        sChannel.m_llTimeOffset = llNow01ms() - llInterface01ms;
        sChannel.m_bTimeOffsetSet = true;
    }

    STCANDATA sCanData;
    memset(&sCanData, 0, sizeof(sCanData));
    sCanData.m_lTickCount.QuadPart = llInterface01ms + sChannel.m_llTimeOffset;

    if (sMsg.MSGTYPE & PCAN_MESSAGE_STATUS)
    {
        return;     // the bus state also comes with the error counters of the error frames
    }
    if (sMsg.MSGTYPE & PCAN_MESSAGE_ERRFRAME)
    {
        // DATA[1] error code capture, DATA[2] receive and DATA[3] transmit error counter; ID 0: counters only
        if (sMsg.LEN >= 4)
        {
            sChannel.m_ucRxErrCount = sMsg.DATA[2];
            sChannel.m_ucTxErrCount = sMsg.DATA[3];
            sChannel.m_ucPeakRxErrCount = max(sChannel.m_ucPeakRxErrCount, sChannel.m_ucRxErrCount);
            sChannel.m_ucPeakTxErrCount = max(sChannel.m_ucPeakTxErrCount, sChannel.m_ucTxErrCount);
        }
        if (sMsg.ID == 0)
        {
            return;
        }
        sCanData.m_ucDataType = ERR_FLAG;
        SERROR_INFO& sError = sCanData.m_uDataInfo.m_sErrInfo;
        sError.m_ucErrType = ERROR_BUS;
        sError.m_ucReg_ErrCap = ucGetErrorCode((sMsg.LEN >= 2) ? sMsg.DATA[1] : 0xE0);
        sError.m_ucRxErrCount = sChannel.m_ucRxErrCount;
        sError.m_ucTxErrCount = sChannel.m_ucTxErrCount;
        sError.m_ucChannel = (UCHAR)(unChannel + 1);
        sError.m_nSubError = 0;
        vWriteIntoClientsBuffer(sCanData);
        return;
    }

    sCanData.m_ucDataType = (sMsg.MSGTYPE & PCAN_MESSAGE_ECHO) ? TX_FLAG : RX_FLAG;
    STCAN_MSG& sCanMsg = sCanData.m_uDataInfo.m_sCANMsg;
    sCanMsg.m_unMsgID = sMsg.ID;
    sCanMsg.m_ucEXTENDED = (sMsg.MSGTYPE & PCAN_MESSAGE_EXTENDED) ? 1 : 0;
    sCanMsg.m_ucRTR = (sMsg.MSGTYPE & PCAN_MESSAGE_RTR) ? 1 : 0;
    sCanMsg.m_ucDataLen = (sMsg.LEN <= 8) ? sMsg.LEN : 8;
    sCanMsg.m_ucChannel = (UCHAR)(unChannel + 1);
    sCanMsg.m_bCANFD = false;
    memcpy(sCanMsg.m_ucData, sMsg.DATA, sCanMsg.m_ucDataLen);
    vWriteIntoClientsBuffer(sCanData);
}

static DWORD WINAPI dwReadThread(LPVOID /*pParam*/)
{
    HANDLE ahWait[CHANNEL_ALLOWED + 1];
    ahWait[0] = sg_hStopEvent;
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        ahWait[i + 1] = sg_asChannels[i].m_hRxEvent;
    }

    for (;;)
    {
        DWORD dwWait = WaitForMultipleObjects(sg_nNoOfChannels + 1, ahWait, FALSE, 500);
        if (dwWait == WAIT_OBJECT_0)
        {
            break;
        }
        // read every channel, also on the time-out in case an event was missed
        for (UINT i = 0; i < sg_nNoOfChannels; i++)
        {
            for (int nRead = 0; nRead < 1000; nRead++)
            {
                TPCANMsg sMsg;
                TPCANTimestamp sTime;
                TPCANStatus nStatus = pfPCAN_Read(sg_asChannels[i].m_hChannel, &sMsg, &sTime);
                if (nStatus != PCAN_ERROR_OK)
                {
                    break;  // queue empty, or a bus state reported instead of a message
                }
                vProcessMessage(i, sMsg, sTime);
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------------
 * The controller class
 * ------------------------------------------------------------------------------------------------------------------ */

class CDIL_CAN_PEAK_PCANBasic : public CBaseDIL_CAN_Controller
{
public:
    HRESULT CAN_PerformInitOperations(void);
    HRESULT CAN_PerformClosureOperations(void);
    HRESULT CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData);
    HRESULT CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT CAN_DeselectHwInterface(void);
    HRESULT CAN_SetConfigData(PSCONTROLLER_DETAILS ConfigFile, int Length);
    HRESULT CAN_StartHardware(void);
    HRESULT CAN_StopHardware(void);
    HRESULT CAN_GetCurrStatus(STATUSMSG& StatusData);
    HRESULT CAN_GetTxMsgBuffer(BYTE*& pouFlxTxMsgBuffer);
    HRESULT CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg);
    HRESULT CAN_GetBusConfigInfo(BYTE* BusInfo);
    HRESULT CAN_GetLastErrorString(std::string& acErrorStr);
    HRESULT CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam);
    HRESULT CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetAppParams(HWND hWndOwner);
    HRESULT CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj);
    HRESULT CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName);
    HRESULT CAN_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus);
    HRESULT CAN_LoadDriverLibrary(void);
    HRESULT CAN_UnloadDriverLibrary(void);
    HRESULT CAN_SetHardwareChannel(PSCONTROLLER_DETAILS, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCount);
};

static CDIL_CAN_PEAK_PCANBasic* sg_pouDIL = nullptr;

USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface)
{
    if (sg_pouDIL == nullptr)
    {
        sg_pouDIL = new CDIL_CAN_PEAK_PCANBasic;
    }
    *ppvInterface = (void*)sg_pouDIL;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0;
    sg_QueryTickCount.QuadPart = 0;
    sg_strLastError = "";
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_LoadDriverLibrary(void)
{
    if (bLoadPCANBasic())
    {
        return S_OK;
    }
    ::MessageBox(sg_hOwnerWnd, sg_strLastError.c_str(), "PEAK PCAN-Basic", MB_OK | MB_ICONWARNING);
    return S_FALSE;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_UnloadDriverLibrary(void)
{
    if (sg_bCurrState != STATE_CONNECTED)
    {
        vUnloadPCANBasic();
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_PerformInitOperations(void)
{
    DWORD dwClientID = 0;
    CAN_RegisterClient(TRUE, dwClientID, CAN_MONITOR_NODE);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_PerformClosureOperations(void)
{
    CAN_StopHardware();
    EnterCriticalSection(&sg_csClients);
    while (sg_unClientCnt > 0)
    {
        bRemoveClient(sg_asClients[0].m_dwClientID);
    }
    LeaveCriticalSection(&sg_csClients);
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    HRESULT hResult = S_FALSE;
    EnterCriticalSection(&sg_csClients);
    if (ClientID != 0)
    {
        UINT unIndex;
        if (bGetClientIndex(ClientID, unIndex))
        {
            SClientBufMap& sClient = sg_asClients[unIndex];
            if (bAction == MSGBUF_ADD)
            {
                if (pBufObj != nullptr && sClient.m_unBufCount < MAX_BUFF_ALLOWED)
                {
                    hResult = S_OK;
                    for (UINT i = 0; i < sClient.m_unBufCount; i++)
                    {
                        if (sClient.m_apClientBuf[i] == pBufObj)
                        {
                            hResult = ERR_BUFFER_EXISTS;
                        }
                    }
                    if (hResult == S_OK)
                    {
                        sClient.m_apClientBuf[sClient.m_unBufCount++] = pBufObj;
                    }
                }
            }
            else if (bAction == MSGBUF_CLEAR)
            {
                if (pBufObj != nullptr)
                {
                    for (UINT i = 0; i < sClient.m_unBufCount; i++)
                    {
                        if (sClient.m_apClientBuf[i] == pBufObj)
                        {
                            sClient.m_apClientBuf[i] = sClient.m_apClientBuf[sClient.m_unBufCount - 1];
                            sClient.m_apClientBuf[--sClient.m_unBufCount] = nullptr;
                            break;
                        }
                    }
                }
                else
                {
                    memset(sClient.m_apClientBuf, 0, sizeof(sClient.m_apClientBuf));
                    sClient.m_unBufCount = 0;
                }
                hResult = S_OK;
            }
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    else if (bAction == MSGBUF_CLEAR)
    {
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            memset(sg_asClients[i].m_apClientBuf, 0, sizeof(sg_asClients[i].m_apClientBuf));
            sg_asClients[i].m_unBufCount = 0;
        }
        hResult = S_OK;
    }
    LeaveCriticalSection(&sg_csClients);
    return hResult;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    HRESULT hResult = S_FALSE;
    EnterCriticalSection(&sg_csClients);
    if (bRegister)
    {
        UINT unIndex;
        if (bGetClientIndexByName(pacClientName, unIndex))
        {
            ClientID = sg_asClients[unIndex].m_dwClientID;
            hResult = ERR_CLIENT_EXISTS;
        }
        else if (sg_unClientCnt >= MAX_CLIENT_ALLOWED)
        {
            hResult = ERR_NO_MORE_CLIENT_ALLOWED;
        }
        else
        {
            ClientID = (strcmp(pacClientName, CAN_MONITOR_NODE) == 0) ? CAN_MONITOR_CLIENT_ID : dwGetFreeClientID();
            SClientBufMap& sClient = sg_asClients[sg_unClientCnt++];
            sClient = SClientBufMap();
            sClient.m_dwClientID = ClientID;
            strncpy_s(sClient.m_acClientName, pacClientName, _TRUNCATE);
            hResult = S_OK;
        }
    }
    else
    {
        hResult = bRemoveClient(ClientID) ? S_OK : ERR_NO_CLIENT_EXIST;
    }
    LeaveCriticalSection(&sg_csClients);
    return hResult;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return WARN_DUMMY_API;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_ListHwInterfaces(INTERFACE_HW_LIST& asSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (!bLoadPCANBasic())
    {
        return NO_HW_INTERFACE;
    }
    std::vector<SPcanHardware> vecHardware = vecScanHardware();
    if (vecHardware.empty())
    {
        sg_strLastError = "No PEAK CAN interface found. Check that it is connected and that the PEAK-System device "
                          "driver is installed.";
        ::MessageBox(sg_hOwnerWnd, sg_strLastError.c_str(), "PEAK PCAN-Basic", MB_OK | MB_ICONWARNING);
        return NO_HW_INTERFACE;
    }

    std::vector<int> vecSelection(max(vecHardware.size(), (size_t)CHANNEL_ALLOWED) + 1, -1);
    int nSelected = 0;

    // a configuration that is being loaded names its channels: no dialog when they are all present
    bool bFromConfiguration = false;
    if ((InitData != nullptr) && (nCount > 0) && (nCount <= (INT)CHANNEL_ALLOWED))
    {
        bFromConfiguration = true;
        for (INT i = 0; i < nCount && bFromConfiguration; i++)
        {
            int nIndex = nFindHardware(vecHardware, InitData[i].m_omHardwareDesc);
            if ((nIndex < 0) || (std::find(vecSelection.begin(), vecSelection.end(), nIndex) != vecSelection.end()))
            {
                bFromConfiguration = false;
            }
            else
            {
                vecSelection[i] = nIndex;
            }
        }
        if (bFromConfiguration)
        {
            nSelected = nCount;
        }
        else
        {
            std::fill(vecSelection.begin(), vecSelection.end(), -1);
        }
    }

    std::vector<INTERFACE_HW> vecInterfaces(vecHardware.size());
    for (size_t i = 0; i < vecHardware.size(); i++)
    {
        vecInterfaces[i].m_dwIdInterface = dwChannelNumber(vecHardware[i].m_hChannel);
        vecInterfaces[i].m_dwVendor = vecHardware[i].m_hChannel;
        vecInterfaces[i].m_acDescription = vecHardware[i].m_strDescription;
        vecInterfaces[i].m_acNameInterface = vecHardware[i].m_strDescription;
        vecInterfaces[i].m_acDeviceName = vecHardware[i].m_strFirmware;
    }

    if (!bFromConfiguration)
    {
        // the channels selected now are shown as selected
        for (UINT i = 0; i < sg_nNoOfChannels; i++)
        {
            int nIndex = nFindHardware(vecHardware, sg_asChannels[i].m_strDescription);
            if (nIndex >= 0)
            {
                vecSelection[nSelected++] = nIndex;
            }
        }

        CWnd objMainWnd;
        objMainWnd.Attach(sg_hOwnerWnd);
        CPCANBasicSettings ouSettings;
        CHardwareListingCAN HwList(&vecInterfaces[0], (int)vecInterfaces.size(), &vecSelection[0], CAN, CHANNEL_ALLOWED,
                                   &objMainWnd, InitData, &ouSettings);
        INT_PTR nRet = HwList.DoModal();
        objMainWnd.Detach();
        if (nRet != IDOK)
        {
            return HW_INTERFACE_NO_SEL;
        }
        nSelected = HwList.nGetSelectedList(&vecSelection[0]);
        if (nSelected <= 0)
        {
            return HW_INTERFACE_NO_SEL;
        }
    }

    vSetChannels(vecHardware, &vecSelection[0], nSelected);
    nCount = (INT)sg_nNoOfChannels;
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        asSelHwInterface[i].m_dwIdInterface = sg_asChannels[i].m_hChannel;
        asSelHwInterface[i].m_acNameInterface = sg_asChannels[i].m_strDescription;
        asSelHwInterface[i].m_acDescription = sg_asChannels[i].m_strDescription;
        if (InitData != nullptr)
        {
            InitData[i].m_omHardwareDesc = sg_asChannels[i].m_strDescription;
        }
    }
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SelectHwInterface(const INTERFACE_HW_LIST& /*asSelHwInterface*/, INT /*nCount*/)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_LISTED, ERR_IMPROPER_STATE);
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_DeselectHwInterface(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SetConfigData(PSCONTROLLER_DETAILS ConfigFile, int Length)
{
    if (ConfigFile == nullptr)
    {
        return S_FALSE;
    }
    // some callers pass the size in bytes instead of the number of channels
    int nChannels = ((Length > 0) && (Length <= defNO_OF_CHANNELS)) ? Length : defNO_OF_CHANNELS;
    for (int i = 0; i < nChannels; i++)
    {
        sg_asControllerDetails[i] = ConfigFile[i];
    }
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        ConfigFile[i].m_omHardwareDesc = sg_asChannels[i].m_strDescription;
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS psDetails, DWORD /*dwDriverId*/,
        bool bIsHardwareListed, unsigned int unChannelCount)
{
    if ((psDetails == nullptr) || (sg_bCurrState == STATE_CONNECTED) || !bLoadPCANBasic())
    {
        return S_OK;
    }
    if (unChannelCount > CHANNEL_ALLOWED)
    {
        unChannelCount = CHANNEL_ALLOWED;
    }
    std::vector<SPcanHardware> vecHardware = vecScanHardware();
    std::vector<int> vecSelection;
    for (unsigned int i = 0; i < unChannelCount; i++)
    {
        const std::string& strDescription = psDetails[i].m_omHardwareDesc;
        if (strDescription.empty() || (strDescription == "Simulation"))
        {
            break;
        }
        int nIndex = nFindHardware(vecHardware, strDescription);
        if (nIndex < 0)
        {
            // a channel of the configuration is not there: keep the present selection
            return bIsHardwareListed ? S_FALSE : S_OK;
        }
        vecSelection.push_back(nIndex);
    }
    if (!vecSelection.empty())
    {
        vSetChannels(vecHardware, &vecSelection[0], (int)vecSelection.size());
    }
    return S_OK;
}

static void vCloseChannels()
{
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        SPcanChannel& sChannel = sg_asChannels[i];
        if (sChannel.m_bInitialized)
        {
            pfPCAN_Uninitialize(sChannel.m_hChannel);
            sChannel.m_bInitialized = false;
        }
        if (sChannel.m_hRxEvent != nullptr)
        {
            CloseHandle(sChannel.m_hRxEvent);
            sChannel.m_hRxEvent = nullptr;
        }
    }
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_StartHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);
    if ((sg_nNoOfChannels == 0) || !bLoadPCANBasic())
    {
        return ERR_LOAD_HW_INTERFACE;
    }
    sg_strLastError = "";

    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        SPcanChannel& sChannel = sg_asChannels[i];
        const SCONTROLLER_DETAILS& sDetails = sg_asControllerDetails[i];

        // listen-only has to be set before the channel is initialised
        DWORD dwListenOnly = sDetails.m_bPassiveMode ? PCAN_PARAMETER_ON : PCAN_PARAMETER_OFF;
        TPCANStatus nStatus = pfPCAN_SetValue(sChannel.m_hChannel, PCAN_LISTEN_ONLY, &dwListenOnly, sizeof(dwListenOnly));
        if ((nStatus != PCAN_ERROR_OK) && sDetails.m_bPassiveMode)
        {
            sg_strLastError = sChannel.m_strDescription + ": listen-only mode not available. " + strStatusText(nStatus);
            vCloseChannels();
            return ERR_LOAD_HW_INTERFACE;
        }

        nStatus = pfPCAN_Initialize(sChannel.m_hChannel, wGetBaudrate(sDetails), 0, 0, 0);
        if (nStatus != PCAN_ERROR_OK)
        {
            sg_strLastError = sChannel.m_strDescription + ": " + strStatusText(nStatus);
            vCloseChannels();
            return ERR_LOAD_HW_INTERFACE;
        }
        sChannel.m_bInitialized = true;

        DWORD dwOn = PCAN_PARAMETER_ON;
        pfPCAN_SetValue(sChannel.m_hChannel, PCAN_ALLOW_ERROR_FRAMES, &dwOn, sizeof(dwOn));   // not all interfaces have it

        sChannel.m_hRxEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (sChannel.m_hRxEvent != nullptr)
        {
            pfPCAN_SetValue(sChannel.m_hChannel, PCAN_RECEIVE_EVENT, &sChannel.m_hRxEvent, sizeof(sChannel.m_hRxEvent));
        }
        sChannel.m_bTimeOffsetSet = false;
        sChannel.m_llTimeOffset = 0;
        sChannel.m_ucTxErrCount = sChannel.m_ucRxErrCount = 0;
        sChannel.m_ucPeakTxErrCount = sChannel.m_ucPeakRxErrCount = 0;
    }

    // reference of the time stamps: the system time now, and the same moment in 0.1 ms of the performance counter
    GetLocalTime(&sg_CurrSysTime);
    QueryPerformanceCounter(&sg_QueryTickCount);
    sg_TimeStamp = (UINT64)llNow01ms();

    sg_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    sg_hReadThread = CreateThread(nullptr, 0, dwReadThread, nullptr, 0, nullptr);
    if ((sg_hStopEvent == nullptr) || (sg_hReadThread == nullptr))
    {
        sg_strLastError = "The read thread of the PEAK driver could not be started.";
        if (sg_hStopEvent != nullptr)
        {
            CloseHandle(sg_hStopEvent);
            sg_hStopEvent = nullptr;
        }
        vCloseChannels();
        return ERR_LOAD_HW_INTERFACE;
    }
    SetThreadPriority(sg_hReadThread, THREAD_PRIORITY_ABOVE_NORMAL);

    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_StopHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    if (sg_hReadThread != nullptr)
    {
        SetEvent(sg_hStopEvent);
        if (WaitForSingleObject(sg_hReadThread, 5000) != WAIT_OBJECT_0)
        {
            TerminateThread(sg_hReadThread, 0);
        }
        CloseHandle(sg_hReadThread);
        sg_hReadThread = nullptr;
    }
    if (sg_hStopEvent != nullptr)
    {
        CloseHandle(sg_hStopEvent);
        sg_hStopEvent = nullptr;
    }
    vCloseChannels();
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetTxMsgBuffer(BYTE*& /*pouFlxTxMsgBuffer*/)
{
    return WARN_DUMMY_API;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    UINT unIndex;
    EnterCriticalSection(&sg_csClients);
    bool bClientExists = bGetClientIndex(dwClientID, unIndex);
    LeaveCriticalSection(&sg_csClients);
    if (!bClientExists)
    {
        return ERR_NO_CLIENT_EXIST;
    }
    if ((sCanTxMsg.m_ucChannel < 1) || (sCanTxMsg.m_ucChannel > sg_nNoOfChannels))
    {
        return ERR_INVALID_CHANNEL;
    }

    TPCANMsg sMsg;
    memset(&sMsg, 0, sizeof(sMsg));
    sMsg.ID = sCanTxMsg.m_unMsgID;
    sMsg.MSGTYPE = (TPCANMessageType)((sCanTxMsg.m_ucEXTENDED ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD) |
                                      (sCanTxMsg.m_ucRTR ? PCAN_MESSAGE_RTR : 0));
    sMsg.LEN = (sCanTxMsg.m_ucDataLen <= 8) ? sCanTxMsg.m_ucDataLen : 8;
    memcpy(sMsg.DATA, sCanTxMsg.m_ucData, sMsg.LEN);

    TPCANStatus nStatus = pfPCAN_Write(sg_asChannels[sCanTxMsg.m_ucChannel - 1].m_hChannel, &sMsg);
    if (nStatus != PCAN_ERROR_OK)
    {
        sg_strLastError = sg_asChannels[sCanTxMsg.m_ucChannel - 1].m_strDescription + ": " + strStatusText(nStatus);
        return S_FALSE;
    }

    STCANDATA sCanData;
    memset(&sCanData, 0, sizeof(sCanData));
    sCanData.m_ucDataType = TX_FLAG;
    sCanData.m_lTickCount.QuadPart = llNow01ms();
    sCanData.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    sCanData.m_uDataInfo.m_sCANMsg.m_ucDataLen = sMsg.LEN;
    sCanData.m_uDataInfo.m_sCANMsg.m_bCANFD = false;
    vWriteIntoClientsBuffer(sCanData, dwClientID);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetBusConfigInfo(BYTE* /*BusInfo*/)
{
    return WARN_DUMMY_API;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetLastErrorString(std::string& acErrorStr)
{
    if (sg_strLastError.empty())
    {
        return WARN_DUMMY_API;
    }
    acErrorStr = sg_strLastError;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = (LONG)sg_nNoOfChannels;
            return S_OK;
        case DRIVER_STATUS:
            lParam = (sg_hPCANBasic != nullptr);
            return S_OK;
        case HW_MODE:
            if (nChannel < sg_nNoOfChannels)
            {
                lParam = sg_asControllerDetails[nChannel].m_bPassiveMode ? defMODE_PASSIVE : defMODE_ACTIVE;
            }
            else
            {
                lParam = defCONTROLLER_BUSOFF + 1;     // unknown
            }
            return S_OK;
        case CON_TEST:
            lParam = TRUE;
            return S_OK;
        default:
            return S_FALSE;
    }
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_SetControllerParams(int /*nValue*/, ECONTR_PARAM /*eContrparam*/)
{
    // listen-only is chosen per channel in the "Advanced" settings of the hardware selection
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_PCANBasic::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam)
{
    if (nChannel >= sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }
    const SPcanChannel& sChannel = sg_asChannels[nChannel];
    if (eContrParam == PEAK_ERR_CNT)
    {
        sErrorCnt.m_ucTxErrCount = sChannel.m_ucPeakTxErrCount;
        sErrorCnt.m_ucRxErrCount = sChannel.m_ucPeakRxErrCount;
    }
    else
    {
        sErrorCnt.m_ucTxErrCount = sChannel.m_ucTxErrCount;
        sErrorCnt.m_ucRxErrCount = sChannel.m_ucRxErrCount;
    }
    return S_OK;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Hardware selection dialog: baud rate check and "Advanced" button
 * ------------------------------------------------------------------------------------------------------------------ */

double CPCANBasicSettings::vValidateBaudRate(double dBaudRate, int /*nChannel*/, UINT /*nClock*/)
{
    if (dBaudRate < 5000)
    {
        return 5000;
    }
    if (dBaudRate > 1000000)
    {
        return 1000000;
    }
    return dBaudRate;
}

int CPCANBasicSettings::InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelected)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if ((pControllerDetails == nullptr) || (nSelected >= nCount))
    {
        return WARN_INITDAT_NCONFIRM;
    }
    CString omName;
    omName.Format("Channel %u: %s", nSelected + 1, pControllerDetails[nSelected].m_omHardwareDesc.c_str());
    CPCANBasicAdvancedDlg ouDlg(omName, pControllerDetails[nSelected].m_bPassiveMode ? TRUE : FALSE);
    if (ouDlg.DoModal() != IDOK)
    {
        return WARN_INITDAT_NCONFIRM;
    }
    pControllerDetails[nSelected].m_bPassiveMode = ouDlg.m_bListenOnly ? 1 : 0;
    return INFO_INITDAT_CONFIRM_CONFIG;
}

CPCANBasicAdvancedDlg::CPCANBasicAdvancedDlg(const CString& omChannelName, BOOL bListenOnly, CWnd* pParent)
    : CDialog(CPCANBasicAdvancedDlg::IDD, pParent), m_bListenOnly(bListenOnly), m_omChannelName(omChannelName)
{
}

void CPCANBasicAdvancedDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_PCAN_LISTEN_ONLY, m_bListenOnly);
}

BOOL CPCANBasicAdvancedDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetDlgItemText(IDC_PCAN_CHANNEL_NAME, m_omChannelName);
    return TRUE;
}
