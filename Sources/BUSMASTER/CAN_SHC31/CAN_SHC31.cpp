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
 * \file           CAN_SHC31/CAN_SHC31.cpp
 * \brief          BusMaster DIL driver for the dsdtech SH-C31x gs_usb adapters
 *
 * \section DESCRIPTION
 * Implements CBaseDIL_CAN_Controller for up to 16 dsdtech SH-C31G USB
 * adapters running the DSD gs_usb firmware v1.4/v1.5. One logical BusMaster
 * channel maps to one WinUSB device handle (device-interface GUID
 * {c15b4308-04d3-11e6-b3ea-6057189e6443}); every adapter exposes exactly one
 * CAN channel. Configuration travels over USB control transfers (gs_usb
 * vendor requests), frames over bulk endpoints in the gs_host_frame format
 * (classic 8-byte and CAN-FD 64-byte payloads), with a 30-slot TX echo pool
 * per device and the device's 1 MHz timestamp wrap-extended into the
 * QueryPerformanceCounter domain.
 *
 * The driver covers registration, client/buffer management, WinUSB/gs_usb
 * enumeration classified by the firmware feature word, bittiming/mode/
 * identify control transfers, per-device overlapped RX threads and TX with
 * the echo-id pool, plus the CHardwareListingCAN dialog integration.
 *
 */

#include "CAN_SHC31_stdafx.h"
#undef _MP                 /** mbctype.h (via MFC afxv_w32.h) defines _MP 0x02, which
                               corrupts 'struct _MP' in the WinSDK usbspec.h; undef before
                               any USB header is included */
#include <windows.h>
#include <winusb.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <string.h>
#include <string>
#include <vector>

#include "BaseDIL_CAN_Controller.h"
#include "Shc31BitTiming.h"
#include "Shc31GsUsb.h"
#include "Shc31ChannelSettings.h"
#include "DIL_Interface/HardwareListingCAN.h"

#define USAGE_EXPORT
#include "CAN_SHC31_Extern.h"

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "cfgmgr32.lib")

/**
 * \brief         MFC application object required by UseOfMfc=Dynamic
 *
 * Supplies the module state used by AFX_MANAGE_STATE when the hardware
 * listing dialog runs inside this DLL.
 */
class CSHC31App : public CWinApp
{
public:
    CSHC31App();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};

CSHC31App theApp;

BEGIN_MESSAGE_MAP(CSHC31App, CWinApp)
END_MESSAGE_MAP()

/**
 * \brief         Constructs the MFC application object
 *
 * \param         None
 *
 * \return        None
 */
CSHC31App::CSHC31App() {}

/**
 * \brief         Initialises the MFC application object
 *
 * \param         None
 *
 * \return        TRUE for success
 */
BOOL CSHC31App::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

/** gs_usb channel (CAN) device interface GUID, binary form of
    SHC31_GSUSB_DEVICE_INTERFACE_GUID_TXT (Shc31GsUsb.h) */
DEFINE_GUID(SHC31_GSUSB_DEVICE_INTERFACE_GUID,
            0xc15b4308, 0x04d3, 0x11e6, 0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43);

/** Maximum number of adapters (logical channels) managed by this driver */
#define SHC31_MAX_DEVICES                               16U

/** Maximum number of registered clients (matches the CAN_STUB convention) */
#define SHC31_MAX_CLIENTS                               16U

/** Maximum number of message buffers per client (matches CAN_STUB) */
#define SHC31_MAX_BUFFERS                               16U

/** RX thread join timeout in milliseconds during shutdown */
#define SHC31_RX_JOIN_TIMEOUT_MS                        2000U

/** Hardware designation embedded in the enumerated device description */
#define SHC31_HW_DESIGNATION_TXT                        "DSD SH-C31G"

/** Description prefix for unknown gs_usb devices (listed but flagged) */
#define SHC31_GENERIC_HW_TXT                            "gs_usb device"

/** Description fragment marking a device without FD support */
#define SHC31_NO_FD_TXT                                 " (no FD)"

/** Description fragment marking a device without LED identify support */
#define SHC31_NO_IDENTIFY_TXT                           " (no identify)"

/** printf fragment for the user id in the device description */
#define SHC31_DESC_ID_FMT                               " (id %lu)"

/** printf fragment for the firmware version in the device description */
#define SHC31_DESC_FW_FMT                               " fw %04lX"

/** Description of the simulated channel (never matched to a device) */
#define SHC31_SIMULATION_DESC_TXT                       "Simulation"

/** Default nominal bitrate when the configuration strings are unusable */
#define SHC31_DEFAULT_NOMINAL_BITRATE                   500000U

/** Default nominal sample point in basis points (87.5 percent) */
#define SHC31_DEFAULT_NOMINAL_SP_BP                     8750U

/** Default data-phase bitrate when the configuration strings are unusable */
#define SHC31_DEFAULT_DATA_BITRATE                      2000000U

/** Default data-phase sample point in basis points (75 percent) */
#define SHC31_DEFAULT_DATA_SP_BP                        7500U

/** Host-to-device vendor request type (direction + vendor + device) */
#define SHC31_USB_REQUEST_TYPE_OUT                      0x40U

/** Device-to-host vendor request type */
#define SHC31_USB_REQUEST_TYPE_IN                       0xC0U


/** Driver state machine states (follows the CAN_STUB convention) */
typedef enum SHC31_STATE_ENUM
{
    SHC31_STATE_PRIMORDIAL = 0,
    SHC31_STATE_RESET,
    SHC31_STATE_REGISTERED,
    SHC31_STATE_INITIALISED,
    SHC31_STATE_CONNECTED
} Shc31StateEnum;

/**
 * One managed device: one logical BusMaster channel.
 */
typedef struct SHC31_DEVICE_STRUCT
{
    HANDLE hDevice;                              /** CreateFile handle, or nullptr when closed */
    WINUSB_INTERFACE_HANDLE hWinUsb;             /** WinUSB interface handle, or nullptr */
    HANDLE hRxThread;                            /** RX thread handle, or nullptr */
    HANDLE hRxExitEvent;                         /** RX thread shutdown event */
    OVERLAPPED sRxOverlapped;                    /** Overlapped bulk IN read block */
    uint8_t aunRxBuffer[SHC31_GSUSB_RX_XFER_SIZE]; /** Bulk IN transfer buffer */
    UCHAR ucPipeIn;                              /** Bulk IN pipe id from the descriptor */
    UCHAR ucPipeOut;                             /** Bulk OUT pipe id from the descriptor */
    bool bBusOpen;                               /** MODE START sent and accepted */
    bool bFdCapable;                             /** Device advertises GS_CAN_FEATURE_FD */
    bool bIdentifyCapable;                       /** Device advertises GS_CAN_FEATURE_IDENTIFY */
    bool bListenOnly;                            /** Open in listen-only mode */
    bool bFailed;                                /** Device-lost flag surfaced by the RX thread */
    std::wstring wstrInterfacePath;              /** WinUSB open key (device interface path) */
    std::string strDescription;                  /** Full description used for persistence */
    uint32_t unFeatureWord;                      /** Feature word from BT_CONST_EXT */
    uint32_t unSwVersion;                        /** sw_version from DEVICE_CONFIG */
    uint32_t unUserId;                           /** User id from GET_USER_ID */
    bool bHasUserId;                             /** unUserId valid (feature bit 6) */
    Shc31TimingStruct sNominalTiming;            /** Nominal timing written at start */
    Shc31TimingStruct sDataTiming;               /** Data-phase timing written at start */
    bool bHasDataTiming;                         /** sDataTiming valid (FD enabled) */
    bool abEchoInFlight[SHC31_GSUSB_ECHO_POOL_MAX]; /** TX echo pool, 30 slots */
    uint32_t unLastRawUs;                        /** Last raw 32-bit device timestamp */
    uint64_t ullWrapOffsetUs;                    /** Accumulated 2^32 microsecond wraps */
    uint64_t ullPrevExtUs;                       /** Previous extended timestamp (restart detection) */
    bool bTimeAnchorValid;                       /** QPC anchor captured */
    uint64_t ullAnchorQpc;                       /** QPC value at the time anchor */
    uint64_t ullAnchorExtUs;                     /** Extended device timestamp at the anchor */
    uint64_t ullLastFrameQpc;                    /** Last converted frame QPC (monotonic clamp) */
} Shc31DeviceStruct;

/**
 * One registered client and its message buffers (follows the CAN_STUB
 * convention, held as driver members instead of file-scope data).
 */
typedef struct SHC31_CLIENT_STRUCT
{
    DWORD dwClientID;
    CBaseCANBufFSE* pClientBuf[SHC31_MAX_BUFFERS];
    std::string strClientName;
    UINT unBufCount;

    SHC31_CLIENT_STRUCT() :
        dwClientID(0),
        pClientBuf(),
        strClientName(""),
        unBufCount(0)
    {
        for (UINT i = 0; i < SHC31_MAX_BUFFERS; i++)
        {
            pClientBuf[i] = nullptr;
        }
    }
} Shc31ClientStruct;

/**
 * Driver class forward declaration for the thread parameter block.
 */
class CDIL_CAN_SHC31;

/**
 * Parameter block handed to the per-device RX thread.
 */
typedef struct SHC31_RX_THREAD_STRUCT
{
    CDIL_CAN_SHC31* pDriver;                     /** Owning driver instance */
    UINT unDeviceIndex;                          /** Index into the device table */
} Shc31RxThreadStruct;

/**
 * CM_Get_Device_Interface_PropertyW exists from Windows 8 on and is missing
 * from the cfgmgr32.dll of Windows 7, so it is resolved dynamically; a static
 * import would prevent this DLL from loading on Windows 7.
 */
typedef CONFIGRET (WINAPI *SHC31_GET_DEVICE_INSTANCE_ID_FN)(LPCWSTR pszDeviceInterface, CONST DEVPROPKEY* PropertyKey, DEVPROPTYPE* PropertyType, PBYTE PropertyBuffer, PULONG PropertyBufferSize, ULONG ulFlags);

/**
 * Free function declarations (Win32 thread entry and the WinUSB layer;
 */
DWORD WINAPI Shc31RxThreadProc(LPVOID pParam);
bool Shc31WinUsbOpen(const wchar_t* pacPath, HANDLE* phDevice, WINUSB_INTERFACE_HANDLE* phWinUsb, UCHAR* pucPipeIn, UCHAR* pucPipeOut);
void Shc31WinUsbClose(HANDLE* phDevice, WINUSB_INTERFACE_HANDLE* phWinUsb);
bool Shc31WinUsbControlOut(WINUSB_INTERFACE_HANDLE hWinUsb, uint8_t ucRequest, uint16_t usIndex, const uint8_t* punData, uint16_t usLength);
bool Shc31WinUsbControlIn(WINUSB_INTERFACE_HANDLE hWinUsb, uint8_t ucRequest, uint16_t usIndex, uint8_t* punData, uint16_t usLength, uint32_t* punLength);
bool Shc31WinUsbWritePipe(WINUSB_INTERFACE_HANDLE hWinUsb, UCHAR ucPipe, const uint8_t* punData, uint32_t unLength);
void Shc31ExtractSerialFromInstanceId(const std::wstring& wstrInstanceId, std::string* pstrSerial);
void Shc31WideToNarrow(const std::wstring& wstrIn, std::string* pstrOut);
bool Shc31GetDeviceInstanceId(const std::wstring& wstrPath, std::wstring* pwstrInstanceId);

/**
 * Driver class: one instance manages all adapters.
 */
class CDIL_CAN_SHC31 : public CBaseDIL_CAN_Controller
{
public:
    CDIL_CAN_SHC31();
    ~CDIL_CAN_SHC31();

    HRESULT CAN_PerformInitOperations(void);
    HRESULT CAN_PerformClosureOperations(void);
    HRESULT CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData);
    HRESULT CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT CAN_DeselectHwInterface(void);
    HRESULT CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length);
    HRESULT CAN_StartHardware(void);
    HRESULT CAN_StopHardware(void);
    HRESULT CAN_GetCurrStatus(STATUSMSG& StatusData);
    HRESULT CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg);
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
    HRESULT CAN_SetHardwareChannel(PSCONTROLLER_DETAILS psControllerDetails, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCnt);

    BOOL bGetClientObj(DWORD dwClientID, UINT& unClientIndex);
    BOOL bClientExist(const char* pacClientName, INT& Index);
    BOOL bClientIdExist(DWORD dwClientId);
    UINT unGetAvailableClientId(void);
    BOOL bRemoveClient(DWORD dwClientId);
    BOOL bIsBufferExists(const Shc31ClientStruct& sClientObj, const CBaseCANBufFSE* pBuf);
    BOOL bRemoveClientBuffer(CBaseCANBufFSE* pRootBufferArray[], UINT& unCount, CBaseCANBufFSE* pBufToRemove);
    HRESULT hEnumerateDevices(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount);
    HRESULT hOpenDeviceAndBus(UINT unDeviceIndex);
    HRESULT hCloseDeviceAndBus(UINT unDeviceIndex);
    HRESULT hConfigureDevice(UINT unDeviceIndex);
    HRESULT hSendIdentify(UINT unDeviceIndex, bool bOn);
    HRESULT hSendIdentifyByDescription(const std::string& strDescription, bool bOn);
    bool bSendIdentifyRequest(UINT unDeviceIndex, bool bOn);
    bool bGetDeviceInfo(const std::string& strDescription, bool* pbFdCapable, bool* pbIdentifyCapable);
    HRESULT hAcquireEchoSlot(Shc31DeviceStruct* psDevice, UINT* punSlot);
    void vReleaseEchoSlot(Shc31DeviceStruct* psDevice, UINT unSlot);
    void vProcessRxData(UINT unDeviceIndex, DWORD dwLength, const LARGE_INTEGER& sFrequency);
    void vDispatchCanData(UINT unDeviceIndex, STCANDATA& sCanData);
    void vConvertTimestamp(Shc31DeviceStruct* psDevice, const Shc31GsUsbRxFrameStruct* psFrame, const LARGE_INTEGER& sFrequency, LARGE_INTEGER* psTick);
    void vSetError(const char* pacError);
    void vSetState(Shc31StateEnum eState);
    Shc31StateEnum eGetState(void);

    CRITICAL_SECTION m_CriticalSection;          /** Guards device table, client map and error string */
    Shc31DeviceStruct m_asDevices[SHC31_MAX_DEVICES];
    Shc31ClientStruct m_asClients[SHC31_MAX_CLIENTS];
    UINT m_unClientCnt;
    UINT m_unDeviceCnt;                          /** Number of enumerated devices */
    UINT m_unSelectedCnt;                        /** Number of selected channels */
    Shc31StateEnum m_eState;
    HWND m_hOwnerWnd;
    std::string m_strError;
    UINT m_unIdentifyChannel;                    /** 0 = none; the channel currently blinking */
};

/** Single instance anchor required by the exported factory function */
CDIL_CAN_SHC31* g_pouDIL_CAN_SHC31 = nullptr;

/**
 * \brief         Constructs the driver instance
 *
 * \param         None
 *
 * \return        None
 */
CDIL_CAN_SHC31::CDIL_CAN_SHC31() :
    m_CriticalSection(),
    m_asDevices(),
    m_asClients(),
    m_unClientCnt(0),
    m_unDeviceCnt(0),
    m_unSelectedCnt(0),
    m_eState(SHC31_STATE_PRIMORDIAL),
    m_hOwnerWnd(nullptr),
    m_strError(""),
    m_unIdentifyChannel(0)
{
    InitializeCriticalSection(&m_CriticalSection);
}

/**
 * \brief         Destroys the driver instance
 *
 * \param         None
 *
 * \return        None
 */
CDIL_CAN_SHC31::~CDIL_CAN_SHC31()
{
    DeleteCriticalSection(&m_CriticalSection);
}

/**
 * \brief         Records a driver error string for CAN_GetLastErrorString
 *
 * \param pacError  Error text
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vSetError(const char* pacError)
{
    /** The error string is written by the RX threads as well as by the
        application threads, so it is guarded; the critical section is
        reentrant for the caller's thread, call sites may hold it already */
    EnterCriticalSection(&m_CriticalSection);
    m_strError = pacError;
    LeaveCriticalSection(&m_CriticalSection);
}

/**
 * \brief         Updates the driver state machine state
 *
 * \param eState    New state
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vSetState(Shc31StateEnum eState)
{
    m_eState = eState;
}

/**
 * \brief         Reads the driver state machine state
 *
 * \param         None
 *
 * \return        Current state
 */
Shc31StateEnum CDIL_CAN_SHC31::eGetState(void)
{
    return m_eState;
}

/**
 * \brief         Finds a client entry by client identifier
 *
 * \param dwClientID     Client identifier to find
 * \param unClientIndex  Output index into the client table
 *
 * \return        TRUE when found
 */
BOOL CDIL_CAN_SHC31::bGetClientObj(DWORD dwClientID, UINT& unClientIndex)
{
    BOOL bResult = FALSE;

    for (UINT i = 0; i < m_unClientCnt; i++)
    {
        if (m_asClients[i].dwClientID == dwClientID)
        {
            unClientIndex = i;
            i = m_unClientCnt;
            bResult = TRUE;
        }
    }

    return bResult;
}

/**
 * \brief         Finds a client entry by name
 *
 * \param pacClientName  Client name to find
 * \param Index          Output index into the client table
 *
 * \return        TRUE when found
 */
BOOL CDIL_CAN_SHC31::bClientExist(const char* pacClientName, INT& Index)
{
    BOOL bResult = FALSE;
    std::string strName = pacClientName;

    for (UINT i = 0; i < m_unClientCnt; i++)
    {
        if (strName == m_asClients[i].strClientName)
        {
            Index = (INT)i;
            i = m_unClientCnt;
            bResult = TRUE;
        }
    }

    return bResult;
}

/**
 * \brief         Checks whether a client identifier is already in use
 *
 * \param dwClientId  Client identifier to check
 *
 * \return        TRUE when the identifier is used
 */
BOOL CDIL_CAN_SHC31::bClientIdExist(DWORD dwClientId)
{
    BOOL bResult = FALSE;

    for (UINT i = 0; i < m_unClientCnt; i++)
    {
        if (m_asClients[i].dwClientID == dwClientId)
        {
            bResult = TRUE;
            i = m_unClientCnt;
        }
    }

    return bResult;
}

/**
 * \brief         Returns the first unused client identifier
 *
 * Clients keep their identifier across removals: the table compacts by
 * swapping the last entry into the hole, so the client count alone cannot
 * decide which identifier is free. Deriving the identifier from the count
 * would hand out an identifier that a removed-and-compacted client still
 * holds (following the CAN_VSCOM dwGetAvailableClientSlot convention).
 *
 * \param         None
 *
 * \return        Free identifier (1 to SHC31_MAX_CLIENTS), or 0 when none is free
 */
UINT CDIL_CAN_SHC31::unGetAvailableClientId(void)
{
    UINT unClientId = 0U;

    for (UINT unCandidate = 1U; (unCandidate <= SHC31_MAX_CLIENTS) && (unClientId == 0U); unCandidate++)
    {
        if (bClientIdExist((DWORD)unCandidate) == FALSE)
        {
            unClientId = unCandidate;
        }
    }

    return unClientId;
}

/**
 * \brief         Removes a client and all its buffers
 *
 * \param dwClientId  Client identifier to remove
 *
 * \return        TRUE when removed
 */
BOOL CDIL_CAN_SHC31::bRemoveClient(DWORD dwClientId)
{
    BOOL bResult = FALSE;

    if (m_unClientCnt > 0U)
    {
        UINT unClientIndex = (UINT)-1;

        if (bGetClientObj(dwClientId, unClientIndex) == TRUE)
        {
            m_asClients[unClientIndex].dwClientID = 0;
            m_asClients[unClientIndex].strClientName = "";

            for (UINT i = 0; i < m_asClients[unClientIndex].unBufCount; i++)
            {
                m_asClients[unClientIndex].pClientBuf[i] = nullptr;
            }
            m_asClients[unClientIndex].unBufCount = 0;

            if ((unClientIndex + 1U) < m_unClientCnt)
            {
                m_asClients[unClientIndex] = m_asClients[m_unClientCnt - 1U];
            }
            m_unClientCnt--;
            bResult = TRUE;
        }
    }

    return bResult;
}

/**
 * \brief         Checks whether a buffer pointer is already registered
 *
 * \param sClientObj  Client entry to search
 * \param pBuf        Buffer pointer to find
 *
 * \return        TRUE when the buffer exists
 */
BOOL CDIL_CAN_SHC31::bIsBufferExists(const Shc31ClientStruct& sClientObj, const CBaseCANBufFSE* pBuf)
{
    BOOL bExist = FALSE;

    for (UINT i = 0; i < sClientObj.unBufCount; i++)
    {
        if (pBuf == sClientObj.pClientBuf[i])
        {
            bExist = TRUE;
            i = sClientObj.unBufCount;
        }
    }

    return bExist;
}

/**
 * \brief         Removes one buffer from a client's buffer array
 *
 * \param pRootBufferArray  Buffer array
 * \param unCount           Buffer count, updated on removal
 * \param pBufToRemove      Buffer pointer to remove
 *
 * \return        TRUE when done
 */
BOOL CDIL_CAN_SHC31::bRemoveClientBuffer(CBaseCANBufFSE* pRootBufferArray[], UINT& unCount, CBaseCANBufFSE* pBufToRemove)
{
    BOOL bReturn = TRUE;

    for (UINT i = 0; i < unCount; i++)
    {
        if (pRootBufferArray[i] == pBufToRemove)
        {
            if (i < (unCount - 1U))
            {
                pRootBufferArray[i] = pRootBufferArray[unCount - 1U];
            }
            unCount--;
        }
    }

    return bReturn;
}

/**
 * \brief         Converts a wide string to a narrow (ASCII) string
 *
 * \param wstrIn   Input wide string
 * \param pstrOut  Output narrow string
 *
 * \return        None
 */
void Shc31WideToNarrow(const std::wstring& wstrIn, std::string* pstrOut)
{
    pstrOut->clear();

    for (size_t i = 0; i < wstrIn.length(); i++)
    {
        wchar_t wch = wstrIn[i];
        pstrOut->push_back((wch <= 0x7F) ? (char)wch : '?');
    }
}

/**
 * \brief         Extracts the trailing serial token from a device instance
 *                identifier such as "USB\VID_1D50&PID_606F\SERIAL"
 *
 * \param wstrInstanceId  Device instance identifier
 * \param pstrSerial      Output serial token (narrow)
 *
 * \return        None
 */
void Shc31ExtractSerialFromInstanceId(const std::wstring& wstrInstanceId, std::string* pstrSerial)
{
    size_t nSlash = wstrInstanceId.rfind(L'\\');

    if (nSlash != std::wstring::npos)
    {
        Shc31WideToNarrow(wstrInstanceId.substr(nSlash + 1U), pstrSerial);
    }
    else
    {
        Shc31WideToNarrow(wstrInstanceId, pstrSerial);
    }

    /** The shared hardware listing uses the description as a CString::Format
        template (getProtocolChannelNameFormat), so a percent sign in a
        device-provided serial would corrupt or crash the list */
    for (size_t i = 0; i < pstrSerial->length(); i++)
    {
        if ((*pstrSerial)[i] == '%')
        {
            (*pstrSerial)[i] = '_';
        }
    }
}

/**
 * \brief         Reads the device instance identifier that publishes a device
 *                interface path
 *
 * CM_Get_Device_Interface_PropertyW requires Windows 8, so it is resolved
 * with GetProcAddress. On Windows 7 it is not available and the instance
 * segment of the interface path
 * ("\\?\usb#vid_vvvv&pid_pppp#instance#{guid}") is used instead. For
 * composite devices that segment is the interface locator and not the
 * serial, so on Windows 7 the derived description is stable per port
 * rather than per adapter; the caller still walks up to the composite
 * device whenever a devnode is resolvable.
 *
 * \param wstrPath         Device interface path
 * \param pwstrInstanceId  Output device instance identifier
 *
 * \return        true when an instance identifier was obtained
 */
bool Shc31GetDeviceInstanceId(const std::wstring& wstrPath, std::wstring* pwstrInstanceId)
{
    bool bResult = false;

    if (pwstrInstanceId != nullptr)
    {
        HMODULE hCfgMgr = GetModuleHandleW(L"cfgmgr32.dll");
        SHC31_GET_DEVICE_INSTANCE_ID_FN pfnProperty = (hCfgMgr != nullptr)
            ? reinterpret_cast<SHC31_GET_DEVICE_INSTANCE_ID_FN>(GetProcAddress(hCfgMgr, "CM_Get_Device_Interface_PropertyW"))
            : nullptr;

        if (pfnProperty != nullptr)
        {
            DEVPROPTYPE ulPropertyType = 0;
            ULONG ulBytes = 0U;

            if (pfnProperty(wstrPath.c_str(), &DEVPKEY_Device_InstanceId, &ulPropertyType, nullptr, &ulBytes, 0U) == CR_BUFFER_SMALL)
            {
                std::vector<wchar_t> omInstanceId((ulBytes / sizeof(wchar_t)) + 1U, L'\0');
                ULONG ulIdBytes = ulBytes;

                if (pfnProperty(wstrPath.c_str(), &DEVPKEY_Device_InstanceId, &ulPropertyType, (PBYTE)omInstanceId.data(), &ulIdBytes, 0U) == CR_SUCCESS)
                {
                    *pwstrInstanceId = omInstanceId.data();
                    bResult = true;
                }
            }
        }
        else
        {
            /** Windows 7 fallback: parse the instance segment out of the path,
                where '#' stands for '\' inside a device instance identifier */
            if (wstrPath.compare(0, 4U, L"\\\\?\\") == 0)
            {
                size_t nHash1 = wstrPath.find(L'#', 4U);
                size_t nHash2 = (nHash1 != std::wstring::npos) ? wstrPath.find(L'#', nHash1 + 1U) : std::wstring::npos;
                size_t nHash3 = (nHash2 != std::wstring::npos) ? wstrPath.find(L'#', nHash2 + 1U) : std::wstring::npos;

                if ((nHash2 != std::wstring::npos) && (nHash3 != std::wstring::npos))
                {
                    std::wstring wstrSegment = wstrPath.substr(nHash2 + 1U, nHash3 - (nHash2 + 1U));

                    for (size_t i = 0; i < wstrSegment.length(); i++)
                    {
                        if (wstrSegment[i] == L'#')
                        {
                            wstrSegment[i] = L'\\';
                        }
                    }

                    *pwstrInstanceId = wstrSegment;
                    bResult = true;
                }
            }
        }
    }

    return bResult;
}

/**
 * \brief         Opens a device interface path and initialises WinUSB,
 *                locating the bulk IN and bulk OUT pipes
 *
 * \param pacPath    Device interface path from the enumeration
 * \param phDevice   Output: CreateFile handle
 * \param phWinUsb   Output: WinUSB interface handle
 * \param pucPipeIn  Output: bulk IN pipe id
 * \param pucPipeOut Output: bulk OUT pipe id
 *
 * \return        TRUE on success
 */
bool Shc31WinUsbOpen(const wchar_t* pacPath, HANDLE* phDevice, WINUSB_INTERFACE_HANDLE* phWinUsb, UCHAR* pucPipeIn, UCHAR* pucPipeOut)
{
    bool bResult = false;
    HANDLE hDevice = CreateFileW(pacPath, GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

    if (hDevice != INVALID_HANDLE_VALUE)
    {
        WINUSB_INTERFACE_HANDLE hWinUsb = nullptr;

        if (WinUsb_Initialize(hDevice, &hWinUsb) != FALSE)
        {
            USB_INTERFACE_DESCRIPTOR sDescriptor;
            UCHAR ucPipeIn = 0U;
            UCHAR ucPipeOut = 0U;
            bool bPipesFound = false;

            if (WinUsb_QueryInterfaceSettings(hWinUsb, 0, &sDescriptor) != FALSE)
            {
                for (UCHAR ucPipe = 0U; ucPipe < sDescriptor.bNumEndpoints; ucPipe++)
                {
                    WINUSB_PIPE_INFORMATION sPipeInfo;

                    if (WinUsb_QueryPipe(hWinUsb, 0, ucPipe, &sPipeInfo) != FALSE)
                    {
                        if (sPipeInfo.PipeType == UsbdPipeTypeBulk)
                        {
                            if ((sPipeInfo.PipeId & 0x80U) != 0U)
                            {
                                ucPipeIn = sPipeInfo.PipeId;
                                bPipesFound = true;
                            }
                            else
                            {
                                ucPipeOut = sPipeInfo.PipeId;
                            }
                        }
                    }
                }
            }

            if (bPipesFound == true)
            {
                *phDevice = hDevice;
                *phWinUsb = hWinUsb;
                *pucPipeIn = ucPipeIn;
                *pucPipeOut = ucPipeOut;
                bResult = true;
            }

            if (bResult == false)
            {
                WinUsb_Free(hWinUsb);
            }
        }

        if (bResult == false)
        {
            CloseHandle(hDevice);
        }
    }

    return bResult;
}

/**
 * \brief         Closes a WinUSB device (frees the interface handle and
 *                closes the device handle)
 *
 * \param phDevice  Device handle pointer, reset on close
 * \param phWinUsb  WinUSB handle pointer, reset on close
 *
 * \return        None
 */
void Shc31WinUsbClose(HANDLE* phDevice, WINUSB_INTERFACE_HANDLE* phWinUsb)
{
    if (*phWinUsb != nullptr)
    {
        (void)WinUsb_Free(*phWinUsb);
        *phWinUsb = nullptr;
    }

    if ((*phDevice != nullptr) && (*phDevice != INVALID_HANDLE_VALUE))
    {
        (void)CloseHandle(*phDevice);
        *phDevice = nullptr;
    }
}

/**
 * \brief         Performs one vendor control transfer (host to device)
 *
 * \param hWinUsb  WinUSB interface handle
 * \param ucRequest  Vendor request number (gs_usb enum)
 * \param usIndex   wIndex (channel number, little-endian on the wire)
 * \param punData   Payload buffer (little-endian u32 arrays)
 * \param usLength  Payload length in bytes
 *
 * \return        TRUE when the transfer completed with the full length
 */
bool Shc31WinUsbControlOut(WINUSB_INTERFACE_HANDLE hWinUsb, uint8_t ucRequest, uint16_t usIndex, const uint8_t* punData, uint16_t usLength)
{
    bool bResult = false;

    if (punData != nullptr)
    {
        WINUSB_SETUP_PACKET sSetup;
        ULONG ulLength = usLength;

        memset(&sSetup, 0, sizeof(sSetup));
        sSetup.RequestType = SHC31_USB_REQUEST_TYPE_OUT;
        sSetup.Request = ucRequest;
        sSetup.Value = 0U;
        sSetup.Index = usIndex;
        sSetup.Length = usLength;

        bResult = (WinUsb_ControlTransfer(hWinUsb, sSetup, const_cast<uint8_t*>(punData), ulLength, &ulLength, nullptr) != FALSE) &&
                  (ulLength == (ULONG)usLength);
    }

    return bResult;
}

/**
 * \brief         Performs one vendor control transfer (device to host)
 *
 * \param hWinUsb  WinUSB interface handle
 * \param ucRequest  Vendor request number (gs_usb enum)
 * \param usIndex   wIndex (channel number)
 * \param punData   Output payload buffer (little-endian u32 arrays)
 * \param usLength  Payload length in bytes
 * \param punLength  Output: bytes actually read
 *
 * \return        TRUE when the transfer completed
 */
bool Shc31WinUsbControlIn(WINUSB_INTERFACE_HANDLE hWinUsb, uint8_t ucRequest, uint16_t usIndex, uint8_t* punData, uint16_t usLength, uint32_t* punLength)
{
    bool bResult = false;

    if (punData != nullptr)
    {
        WINUSB_SETUP_PACKET sSetup;
        ULONG ulLength = usLength;

        memset(&sSetup, 0, sizeof(sSetup));
        sSetup.RequestType = SHC31_USB_REQUEST_TYPE_IN;
        sSetup.Request = ucRequest;
        sSetup.Value = 0U;
        sSetup.Index = usIndex;
        sSetup.Length = usLength;

        if (WinUsb_ControlTransfer(hWinUsb, sSetup, punData, ulLength, &ulLength, nullptr) != FALSE)
        {
            *punLength = (uint32_t)ulLength;
            bResult = true;
        }
    }

    return bResult;
}

/**
 * \brief         Writes one buffer to a bulk OUT pipe (synchronous)
 *
 * \param hWinUsb  WinUSB interface handle
 * \param ucPipe   Bulk OUT pipe id
 * \param punData  Frame buffer
 * \param unLength Frame length in bytes
 *
 * \return        TRUE when the full buffer was written
 */
bool Shc31WinUsbWritePipe(WINUSB_INTERFACE_HANDLE hWinUsb, UCHAR ucPipe, const uint8_t* punData, uint32_t unLength)
{
    bool bResult = false;
    ULONG ulWritten = 0U;

    if ((punData != nullptr) && (WinUsb_WritePipe(hWinUsb, ucPipe, const_cast<uint8_t*>(punData), (ULONG)unLength, &ulWritten, nullptr) != FALSE))
    {
        bResult = (ulWritten == (ULONG)unLength);
    }

    return bResult;
}

/**
 * \brief         RX thread body: one thread per open device with one
 *                overlapped bulk IN read outstanding. Frames are decoded by
 *                the gs_usb codec and dispatched into the client buffers;
 *                TX echo frames release their echo-pool slot.
 *
 * \param pParam  Pointer to a Shc31RxThreadStruct owned by the driver
 *
 * \return        0 on normal exit
 */
DWORD WINAPI Shc31RxThreadProc(LPVOID pParam)
{
    Shc31RxThreadStruct* psThread = (Shc31RxThreadStruct*)pParam;
    CDIL_CAN_SHC31* pDriver = psThread->pDriver;
    UINT unDeviceIndex = psThread->unDeviceIndex;
    Shc31DeviceStruct* psDevice = &(pDriver->m_asDevices[unDeviceIndex]);
    LARGE_INTEGER sFrequency;

    delete psThread;

    (void)QueryPerformanceFrequency(&sFrequency);

    memset(&psDevice->sRxOverlapped, 0, sizeof(psDevice->sRxOverlapped));
    psDevice->sRxOverlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    bool bRun = (psDevice->sRxOverlapped.hEvent != nullptr);

    if (bRun == true)
    {
        if (WinUsb_ReadPipe(psDevice->hWinUsb, psDevice->ucPipeIn, psDevice->aunRxBuffer,
                            SHC31_GSUSB_RX_XFER_SIZE, nullptr, &(psDevice->sRxOverlapped)) == FALSE)
        {
            DWORD dwError = GetLastError();

            if (dwError != ERROR_IO_PENDING)
            {
                pDriver->vSetError("the initial overlapped bulk IN read failed");
                bRun = false;
            }
        }
    }

    while (bRun == true)
    {
        HANDLE ahWait[2] = { psDevice->hRxExitEvent, psDevice->sRxOverlapped.hEvent };
        DWORD dwWait = WaitForMultipleObjects(2U, ahWait, FALSE, INFINITE);
        DWORD dwRead = 0U;

        if (dwWait == WAIT_OBJECT_0)
        {
            bRun = false;
        }
        else if (dwWait == (WAIT_OBJECT_0 + 1U))
        {
            if (GetOverlappedResult(psDevice->hDevice, &(psDevice->sRxOverlapped), &dwRead, TRUE) == FALSE)
            {
                if (GetLastError() != ERROR_OPERATION_ABORTED)
                {
                    /** The failure flags go up before the error string: the
                        string write needs the driver critical section, which
                        a concurrent shutdown takes first */
                    psDevice->bFailed = true;
                    bRun = false;

                    pDriver->vSetError("the bulk IN read failed (device lost)");
                }
                else
                {
                    bRun = false;
                }
            }
            else
            {
                if (dwRead > 0U)
                {
                    pDriver->vProcessRxData(unDeviceIndex, dwRead, sFrequency);
                }

                if (WinUsb_ReadPipe(psDevice->hWinUsb, psDevice->ucPipeIn, psDevice->aunRxBuffer,
                                    SHC31_GSUSB_RX_XFER_SIZE, nullptr, &(psDevice->sRxOverlapped)) == FALSE)
                {
                    DWORD dwError = GetLastError();

                    if (dwError != ERROR_IO_PENDING)
                    {
                        psDevice->bFailed = true;
                        bRun = false;

                        pDriver->vSetError("the overlapped bulk IN read could not be re-issued");
                    }
                }
            }
        }
        else
        {
            bRun = false;
        }
    }

    if (psDevice->sRxOverlapped.hEvent != nullptr)
    {
        (void)CloseHandle(psDevice->sRxOverlapped.hEvent);
        psDevice->sRxOverlapped.hEvent = nullptr;
    }

    return 0U;
}

/**
 * \brief         Allocates a free TX echo-pool slot (0..29) and marks it
 *                in flight. The mark happens before the caller writes the
 *                frame, so the RX thread cannot race the allocation.
 *
 * \param psDevice  Device entry
 * \param punSlot   Output: allocated slot
 *
 * \return        S_OK on success, S_FALSE when all 30 slots are in flight
 */
HRESULT CDIL_CAN_SHC31::hAcquireEchoSlot(Shc31DeviceStruct* psDevice, UINT* punSlot)
{
    HRESULT hResult = S_FALSE;

    for (UINT i = 0; i < SHC31_GSUSB_ECHO_POOL_MAX; i++)
    {
        if (psDevice->abEchoInFlight[i] == false)
        {
            psDevice->abEchoInFlight[i] = true;
            *punSlot = i;
            hResult = S_OK;
            i = SHC31_GSUSB_ECHO_POOL_MAX;
        }
    }

    return hResult;
}

/**
 * \brief         Releases a TX echo-pool slot after completion or error
 *
 * \param psDevice  Device entry
 * \param unSlot    Slot to release
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vReleaseEchoSlot(Shc31DeviceStruct* psDevice, UINT unSlot)
{
    if (unSlot < SHC31_GSUSB_ECHO_POOL_MAX)
    {
        psDevice->abEchoInFlight[unSlot] = false;
    }
}

/**
 * \brief         Converts the device's 1 MHz timestamp into the BusMaster
 *                tick domain (0.1 ms units, the BusEmulation SimENG
 *                convention: QPC ticks * 10000 / frequency) using the
 *                per-device wrap-extended counter and the thread's QPC
 *                anchor
 *
 * \param psDevice    Device entry (timestamp state updated in place)
 * \param psFrame     Decoded frame (bHasTimestamp selects the device path)
 * \param sFrequency  QPC frequency captured by the RX thread
 * \param psTick      Output: tick for STCANDATA::m_lTickCount, in 0.1 ms
 *                    units since boot
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vConvertTimestamp(Shc31DeviceStruct* psDevice, const Shc31GsUsbRxFrameStruct* psFrame, const LARGE_INTEGER& sFrequency, LARGE_INTEGER* psTick)
{
    LARGE_INTEGER sNow;
    uint64_t ullQpc = 0U;

    (void)QueryPerformanceCounter(&sNow);

    if (psFrame->bHasTimestamp == true)
    {
        uint64_t ullExt = Shc31GsUsbExtendTimestamp(psFrame->unTimestampUs, &(psDevice->unLastRawUs), &(psDevice->ullWrapOffsetUs));

        if (psDevice->bTimeAnchorValid == false)
        {
            psDevice->ullAnchorQpc = (uint64_t)sNow.QuadPart;
            psDevice->ullAnchorExtUs = ullExt;
            psDevice->bTimeAnchorValid = true;
        }
        else if (ullExt < psDevice->ullPrevExtUs)
        {
            /** The counter jumped back beyond the codec's wrap guard: the
                device restarted; re-anchor in the QPC domain */
            psDevice->ullAnchorQpc = (uint64_t)sNow.QuadPart;
            psDevice->ullAnchorExtUs = ullExt;
        }
        else
        {
            /** Keep the existing anchor */
        }

        psDevice->ullPrevExtUs = ullExt;
        ullQpc = psDevice->ullAnchorQpc + (((ullExt - psDevice->ullAnchorExtUs) * (uint64_t)sFrequency.QuadPart) / 1000000ULL);
    }
    else
    {
        ullQpc = (uint64_t)sNow.QuadPart;
    }

    if (ullQpc <= psDevice->ullLastFrameQpc)
    {
        ullQpc = psDevice->ullLastFrameQpc + 1U;
    }

    psDevice->ullLastFrameQpc = ullQpc;
    /** The app consumes m_lTickCount as 0.1 ms ticks (vFormatTimeStamp
        formats dwTimeStamp % 10000 as the 0.1 ms digit); raw QPC ticks
        inflate the displayed time by the QPC frequency over 10 kHz */
    psTick->QuadPart = (LONGLONG)((ullQpc * 10000ULL) / (uint64_t)sFrequency.QuadPart);
}

/**
 * \brief         Writes one decoded message into every registered buffer of
 *                every client (BusMaster fans out app-side by client; TX
 *                echoes are delivered with the TX flag, matching the
 *                CAN_VSCOM/MHS convention)
 *
 * \param unDeviceIndex  Device index (for the message channel field)
 * \param sCanData       Message data to dispatch
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vDispatchCanData(UINT unDeviceIndex, STCANDATA& sCanData)
{
    for (UINT i = 0; i < m_unClientCnt; i++)
    {
        for (UINT j = 0; j < m_asClients[i].unBufCount; j++)
        {
            m_asClients[i].pClientBuf[j]->WriteIntoBuffer(&sCanData);
        }
    }

    (void)unDeviceIndex;
}

/**
 * \brief         Maps a firmware error frame onto the BusMaster error
 *                capture code (SERROR_INFO::m_ucReg_ErrCap)
 *
 * \param sFrame  Decoded gs_host_frame (raw id flags and payload kept)
 *
 * \return        One of the CAN_Error_Defs.h capture codes; the firmware
 *                error frame carries the kernel class bits in the raw
 *                can_id (can.c can_parse_error_status), the protocol error
 *                type in data[2], the CRC location in data[3] and the
 *                error counters in data[6]/data[7]
 */
unsigned char Shc31MapErrorCapture(const Shc31GsUsbRxFrameStruct& sFrame)
{
    unsigned char ucCode = OTHER_ERROR_RX;

    if (sFrame.aunData[2] != 0U)
    {
        if ((sFrame.aunData[2] & SHC31_GSUSB_DATA_PROT_STUFF) != 0U)
        {
            ucCode = STUFF_ERROR_RX;
        }
        else if ((sFrame.aunData[2] & SHC31_GSUSB_DATA_PROT_FORM) != 0U)
        {
            ucCode = FORM_ERROR_RX;
        }
        else if (((sFrame.aunData[2] & SHC31_GSUSB_DATA_PROT_BIT0) != 0U) ||
                 ((sFrame.aunData[2] & SHC31_GSUSB_DATA_PROT_BIT1) != 0U))
        {
            ucCode = BIT_ERROR_TX;
        }
        else
        {
            ucCode = OTHER_ERROR_RX;
        }
    }
    else if (sFrame.aunData[3] != 0U)
    {
        ucCode = CRC_ERROR_RX;
    }
    else if ((sFrame.unRawCanId & SHC31_GSUSB_ID_ERR_CLASS_ACK) != 0U)
    {
        ucCode = ACK_ERROR_TX;
    }
    else if ((sFrame.unRawCanId & SHC31_GSUSB_ID_ERR_CLASS_BUSOFF) != 0U)
    {
        ucCode = OTHER_ERROR_TX;
    }
    else
    {
        ucCode = OTHER_ERROR_RX;
    }

    return ucCode;
}

/**
 * \brief         Parses one bulk transfer: decodes the gs_host_frame(s) and
 *                dispatches RX frames, TX echoes and error frames. Without
 *                the pad flag every transfer is exactly one event
 *                (short-packet terminated); back-to-back frames are handled
 *                defensively in case a future firmware change batches them
 *
 * \param unDeviceIndex  Device index
 * \param dwLength       Transfer byte count
 * \param sFrequency     QPC frequency from the RX thread
 *
 * \return        None
 */
void CDIL_CAN_SHC31::vProcessRxData(UINT unDeviceIndex, DWORD dwLength, const LARGE_INTEGER& sFrequency)
{
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);
    uint32_t unOffset = 0U;


    while ((unOffset + SHC31_GSUSB_FRAME_HEADER_SIZE) <= (uint32_t)dwLength)
    {
        Shc31GsUsbRxFrameStruct sFrame;

        /** Clear the record before decoding: the decoder writes the payload
            only when a frame has one, while the error-frame decoding reads
            payload offsets 2, 3, 6 and 7 even for short frames */
        memset(&sFrame, 0, sizeof(sFrame));

        if (Shc31GsUsbDecodeRxFrame(psDevice->aunRxBuffer + unOffset, (uint32_t)dwLength - unOffset, &sFrame) == false)
        {
            unOffset = (uint32_t)dwLength;
        }
        else
        {
            uint32_t unFrameLen = ((sFrame.bFd == true) ? SHC31_GSUSB_FRAME_FD_TS_SIZE : SHC31_GSUSB_FRAME_CLASSIC_TS_SIZE);
            uint32_t unRemaining = (uint32_t)dwLength - unOffset;
            bool bStop = false;


            if (sFrame.EchoId != SHC31_GSUSB_ECHO_ID_RX)
            {
                /** Unknown echo ids are stale or corrupt events; the
                    firmware contract says to ignore them so they never
                    render as phantom TX rows. The in-flight test reads
                    driver state, so it runs under the critical section
                    together with the slot release. */
                if (sFrame.EchoId < SHC31_GSUSB_ECHO_POOL_MAX)
                {
                    EnterCriticalSection(&m_CriticalSection);

                    if (psDevice->abEchoInFlight[sFrame.EchoId] == true)
                    {
                        STCANDATA sCanData;
                        STCAN_MSG sMsg;

                        vReleaseEchoSlot(psDevice, (UINT)sFrame.EchoId);

                        memset(&sCanData, 0, sizeof(sCanData));
                        memset(&sMsg, 0, sizeof(sMsg));

                        sMsg.m_unMsgID = sFrame.MsgId;
                        sMsg.m_ucEXTENDED = (sFrame.bExtended == true) ? 1U : 0U;
                        sMsg.m_ucRTR = (sFrame.bRtr == true) ? 1U : 0U;
                        sMsg.m_ucDataLen = (unsigned char)sFrame.unDataLen;
                        sMsg.m_ucChannel = (unsigned char)(unDeviceIndex + 1U);
                        sMsg.m_bCANFD = sFrame.bFd;

                        /** RTR frames carry no payload, so the data bytes of
                            a remote-frame echo stay zero and only the DLC is
                            shown; data frames are echoed with their payload */
                        if (sFrame.bRtr == false)
                        {
                            for (uint32_t i = 0; i < sFrame.unDataLen; i++)
                            {
                                sMsg.m_ucData[i] = sFrame.aunData[i];
                            }
                        }

                        vConvertTimestamp(psDevice, &sFrame, sFrequency, &(sCanData.m_lTickCount));
                        sCanData.m_ucDataType = TX_FLAG;
                        sCanData.m_uDataInfo.m_sCANMsg = sMsg;

                        vDispatchCanData(unDeviceIndex, sCanData);
                    }

                    LeaveCriticalSection(&m_CriticalSection);
                }
            }
            else
            {
                if ((sFrame.unRawCanId & SHC31_GSUSB_ID_FLAG_ERR) != 0U)
                {
                    /** The firmware pushes kernel-style error frames on
                        controller state transitions and LEC errors; decode
                        them into the BusMaster error event so the window
                        shows the bus condition instead of a phantom data
                        frame (the ERR id bit would otherwise be masked off
                        into an ordinary-looking identifier) */
                    STCANDATA sCanData;

                    memset(&sCanData, 0, sizeof(sCanData));

                    vConvertTimestamp(psDevice, &sFrame, sFrequency, &(sCanData.m_lTickCount));
                    sCanData.m_ucDataType = ERR_FLAG;
                    sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType = ERROR_BUS;
                    sCanData.m_uDataInfo.m_sErrInfo.m_ucReg_ErrCap = Shc31MapErrorCapture(sFrame);
                    sCanData.m_uDataInfo.m_sErrInfo.m_ucTxErrCount = sFrame.aunData[6];
                    sCanData.m_uDataInfo.m_sErrInfo.m_ucRxErrCount = sFrame.aunData[7];
                    sCanData.m_uDataInfo.m_sErrInfo.m_ucChannel = (unsigned char)(unDeviceIndex + 1U);
                    sCanData.m_uDataInfo.m_sErrInfo.m_nSubError = 0;

                    EnterCriticalSection(&m_CriticalSection);
                    vDispatchCanData(unDeviceIndex, sCanData);
                    LeaveCriticalSection(&m_CriticalSection);
                }
                else
                {
                    STCANDATA sCanData;
                    STCAN_MSG sMsg;

                    memset(&sCanData, 0, sizeof(sCanData));
                    memset(&sMsg, 0, sizeof(sMsg));

                    sMsg.m_unMsgID = sFrame.MsgId;
                    sMsg.m_ucEXTENDED = (sFrame.bExtended == true) ? 1U : 0U;
                    sMsg.m_ucRTR = (sFrame.bRtr == true) ? 1U : 0U;
                    sMsg.m_ucDataLen = (unsigned char)sFrame.unDataLen;
                    sMsg.m_ucChannel = (unsigned char)(unDeviceIndex + 1U);
                    sMsg.m_bCANFD = sFrame.bFd;

                    for (uint32_t i = 0; i < sFrame.unDataLen; i++)
                    {
                        sMsg.m_ucData[i] = sFrame.aunData[i];
                    }

                    vConvertTimestamp(psDevice, &sFrame, sFrequency, &(sCanData.m_lTickCount));
                    sCanData.m_ucDataType = RX_FLAG;
                    sCanData.m_uDataInfo.m_sCANMsg = sMsg;

                    EnterCriticalSection(&m_CriticalSection);
                    vDispatchCanData(unDeviceIndex, sCanData);
                    LeaveCriticalSection(&m_CriticalSection);
                }
            }

            /** Stop at an all-zero tail: a further decode of zero padding
                would fabricate an echo_id-0 frame. Real frames are followed
                by payload or the transfer end, never by an all-zero run. */
            if (unFrameLen >= unRemaining)
            {
                bStop = true;
            }
            else
            {
                bStop = true;

                for (uint32_t i = unOffset + unFrameLen; i < (uint32_t)dwLength; i++)
                {
                    if (psDevice->aunRxBuffer[i] != 0U)
                    {
                        bStop = false;
                        i = (uint32_t)dwLength;
                    }
                }
            }

            unOffset = (bStop == true) ? (uint32_t)dwLength : (unOffset + unFrameLen);
        }
    }
}

/**
 * \brief         Factory function required by the BusMaster DIL contract
 *
 * \param ppvInterface  Output: pointer to the CBaseDIL_CAN_Controller instance
 *
 * \return        S_OK for success, S_FALSE for failure
 */
USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface)
{
    HRESULT hResult = S_OK;

    if (g_pouDIL_CAN_SHC31 == nullptr)
    {
        g_pouDIL_CAN_SHC31 = new CDIL_CAN_SHC31;

        if (g_pouDIL_CAN_SHC31 == nullptr)
        {
            hResult = S_FALSE;
        }
    }

    *ppvInterface = (void*)g_pouDIL_CAN_SHC31;

    return hResult;
}

/**
 * \brief         Performs initial operations: resets the device and client
 *                tables
 *
 * \param         None
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_PerformInitOperations(void)
{
    HRESULT hResult = S_FALSE;

    EnterCriticalSection(&m_CriticalSection);

    /** In the DIL flow CAN_SetAppParams runs first and already leaves the
        reset state, so both entry states are accepted here; repeated calls
        stay idempotent */
    if ((m_eState == SHC31_STATE_PRIMORDIAL) || (m_eState == SHC31_STATE_RESET))
    {
        m_unClientCnt = 0;
        m_unDeviceCnt = 0;
        m_unSelectedCnt = 0;
        m_unIdentifyChannel = 0;
        m_strError = "";
        vSetState(SHC31_STATE_RESET);
        hResult = S_OK;
    }
    else
    {
        vSetError("improper state for PerformInitOperations");
    }

    LeaveCriticalSection(&m_CriticalSection);

    return hResult;
}

/**
 * \brief         Performs closure operations: stops the hardware, removes
 *                all clients and returns to the primordial state
 *
 * \param         None
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_PerformClosureOperations(void)
{
    HRESULT hResult = S_OK;

    /** Best effort: cancel the LED blink on every enumerated adapter; the
        firmware keeps the blink state until it receives IDENTIFY-off */
    for (UINT i = 0; i < m_unDeviceCnt; i++)
    {
        if (m_asDevices[i].bIdentifyCapable == true)
        {
            (void)bSendIdentifyRequest(i, false);
        }
    }
    m_unIdentifyChannel = 0U;

    (void)CAN_StopHardware();

    EnterCriticalSection(&m_CriticalSection);

    for (UINT i = 0; i < m_unClientCnt; i++)
    {
        m_asClients[i].dwClientID = 0;
        m_asClients[i].strClientName = "";

        for (UINT b = 0; b < m_asClients[i].unBufCount; b++)
        {
            m_asClients[i].pClientBuf[b] = nullptr;
        }
        m_asClients[i].unBufCount = 0;
    }
    m_unClientCnt = 0;
    vSetState(SHC31_STATE_PRIMORDIAL);

    LeaveCriticalSection(&m_CriticalSection);

    return hResult;
}

/**
 * \brief         Gets the time mode mapping. The frame ticks are 0.1 ms
 *                ticks since boot (QPC converted), so the mapping returned
 *                here uses the same conversion and domain
 *
 * \param CurrSysTime    Output: current system time
 * \param TimeStamp      Output: 0.1 ms tick for the connect instant
 * \param QueryTickCount Output: raw performance counter
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    HRESULT hResult = S_OK;
    LARGE_INTEGER sCounter = { 0 };
    LARGE_INTEGER sFrequency = { 0 };

    GetLocalTime(&CurrSysTime);
    (void)QueryPerformanceCounter(&sCounter);
    (void)QueryPerformanceFrequency(&sFrequency);

    QueryTickCount = sCounter;
    TimeStamp = (UINT64)(((uint64_t)sCounter.QuadPart * 10000ULL) / (uint64_t)sFrequency.QuadPart);

    return hResult;
}

/**
 * \brief         Enumerates gs_usb device interfaces by GUID via
 *                CM_Get_Device_Interface_List, probes each with DEVICE_
 *                CONFIG / BT_CONST_EXT / GET_USER_ID and classifies the
 *                firmware from its feature word
 *
 * \param sSelHwInterface  Output interface list
 * \param nCount           Output: number of enumerated devices
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::hEnumerateDevices(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount)
{
    HRESULT hResult = S_FALSE;
    GUID sInterfaceGuid = SHC31_GSUSB_DEVICE_INTERFACE_GUID;
    ULONG ulListSize = 0U;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(&ulListSize, &sInterfaceGuid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    m_unDeviceCnt = 0;

    /** Entries from a previous enumeration must not survive: devices beyond
        the new count would keep their interface path and then be opened by
        CAN_StartHardware even though they are not selected. Safe because
        CAN_ListHwInterfaces refuses to enumerate while devices are open. */
    for (UINT unDevice = 0U; unDevice < SHC31_MAX_DEVICES; unDevice++)
    {
        m_asDevices[unDevice] = Shc31DeviceStruct();
    }

    if ((cr == CR_SUCCESS) && (ulListSize > 0U))
    {
        std::vector<wchar_t> omList(ulListSize, L'\0');

        cr = CM_Get_Device_Interface_ListW(&sInterfaceGuid, nullptr, omList.data(), ulListSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        if (cr == CR_SUCCESS)
        {
            const wchar_t* pacPath = omList.data();

            while ((*pacPath != L'\0') && (m_unDeviceCnt < SHC31_MAX_DEVICES))
            {
                std::wstring wstrPath = pacPath;
                std::wstring wstrInstanceId;

                pacPath += wstrPath.length() + 1U;

                (void)Shc31GetDeviceInstanceId(wstrPath, &wstrInstanceId);

                /** Probe: open WinUSB, read the classification control
                    registers, close (classify by feature bits) */
                HANDLE hDevice = nullptr;
                WINUSB_INTERFACE_HANDLE hWinUsb = nullptr;
                UCHAR ucPipeIn = 0U;
                UCHAR ucPipeOut = 0U;

                if (Shc31WinUsbOpen(wstrPath.c_str(), &hDevice, &hWinUsb, &ucPipeIn, &ucPipeOut) == true)
                {
                    uint8_t aunConfig[SHC31_GSUSB_DEVICE_CONFIG_SIZE] = { 0U };
                    uint8_t aunBtConst[SHC31_GSUSB_BT_CONST_EXT_SIZE] = { 0U };
                    uint8_t aunUserId[4] = { 0U };
                    uint32_t unConfigLen = 0U;
                    uint32_t unBtConstLen = 0U;
                    uint32_t unUserIdLen = 0U;
                    Shc31DeviceStruct* psDevice = &(m_asDevices[m_unDeviceCnt]);
                    char acDescription[512] = { '\0' };
                    std::string strSerial = "";

                    if ((Shc31WinUsbControlIn(hWinUsb, SHC31_GSUSB_BREQ_DEVICE_CONFIG, 0U, aunConfig, sizeof(aunConfig), &unConfigLen) == true) &&
                            (unConfigLen >= (SHC31_GSUSB_DEVICE_CONFIG_SW_VERSION_OFFSET + 4U)))
                    {
                        psDevice->unSwVersion = Shc31GsUsbGetU32(aunConfig + SHC31_GSUSB_DEVICE_CONFIG_SW_VERSION_OFFSET);
                    }

                    /** v1.4/v1.5 answers BT_CONST_EXT with 72 bytes; a short
                        (68-byte) request STALLs on this firmware. Fall back to
                        the classic BT_CONST answer so legacy stock units are
                        still classified by their feature word. */
                    if ((Shc31WinUsbControlIn(hWinUsb, SHC31_GSUSB_BREQ_BT_CONST_EXT, 0U, aunBtConst, sizeof(aunBtConst), &unBtConstLen) == true) &&
                            (unBtConstLen >= 4U))
                    {
                        psDevice->unFeatureWord = Shc31GsUsbGetU32(aunBtConst + 0);
                    }
                    else if ((Shc31WinUsbControlIn(hWinUsb, SHC31_GSUSB_BREQ_BT_CONST, 0U, aunBtConst, SHC31_GSUSB_BT_CONST_SIZE, &unBtConstLen) == true) &&
                             (unBtConstLen >= 4U))
                    {
                        psDevice->unFeatureWord = Shc31GsUsbGetU32(aunBtConst + 0);
                    }

                    if (psDevice->unFeatureWord != 0U)
                    {
                        psDevice->bFdCapable = ((psDevice->unFeatureWord & SHC31_GSUSB_FEATURE_FD) != 0U);
                        psDevice->bIdentifyCapable = ((psDevice->unFeatureWord & SHC31_GSUSB_FEATURE_IDENTIFY) != 0U);
                    }

                    if ((Shc31WinUsbControlIn(hWinUsb, SHC31_GSUSB_BREQ_GET_USER_ID, 0U, aunUserId, sizeof(aunUserId), &unUserIdLen) == true) &&
                            (unUserIdLen >= 4U))
                    {
                        psDevice->unUserId = Shc31GsUsbGetU32(aunUserId + 0);
                        psDevice->bHasUserId = true;
                    }

                    Shc31WinUsbClose(&hDevice, &hWinUsb);

                    /** The interface instance id ends in the USB topology
                        locator (B&25DEFB00&0&0000), not the device serial.
                        Walk up to the composite device so the description is
                        stable across ports and replugs. */
                    std::wstring wstrSerialSource = wstrInstanceId;
                    DEVINST dnDevInst = 0U;

                    if (CM_Locate_DevNodeW(&dnDevInst, const_cast<DEVINSTID_W>(wstrInstanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
                    {
                        DEVINST dnParent = 0U;

                        if (CM_Get_Parent(&dnParent, dnDevInst, 0U) == CR_SUCCESS)
                        {
                            wchar_t acParentId[MAX_DEVICE_ID_LEN] = { L'\0' };

                            if (CM_Get_Device_IDW(dnParent, acParentId, MAX_DEVICE_ID_LEN, 0U) == CR_SUCCESS)
                            {
                                wstrSerialSource = acParentId;
                            }
                        }
                    }

                    Shc31ExtractSerialFromInstanceId(wstrSerialSource, &strSerial);

                    const char* pacDesignation = ((psDevice->unFeatureWord == SHC31_GSUSB_FEATURE_WORD_DSD_V14) ||
                                                  (psDevice->unFeatureWord == SHC31_GSUSB_FEATURE_WORD_DSD_V15))
                                                 ? SHC31_HW_DESIGNATION_TXT : SHC31_GENERIC_HW_TXT;
                    INT nPos = sprintf_s(acDescription, sizeof(acDescription), "%s %s", pacDesignation, strSerial.c_str());

                    if ((nPos > 0) && (psDevice->bHasUserId == true))
                    {
                        nPos += sprintf_s(acDescription + nPos, sizeof(acDescription) - (size_t)nPos, SHC31_DESC_ID_FMT, (unsigned long)psDevice->unUserId);
                    }

                    if ((nPos > 0) && (psDevice->unSwVersion != 0U))
                    {
                        nPos += sprintf_s(acDescription + nPos, sizeof(acDescription) - (size_t)nPos, SHC31_DESC_FW_FMT, (unsigned long)psDevice->unSwVersion);
                    }

                    if (nPos > 0)
                    {
                        if (psDevice->bFdCapable == false)
                        {
                            (void)strcat_s(acDescription, sizeof(acDescription), SHC31_NO_FD_TXT);
                        }

                        if (psDevice->bIdentifyCapable == false)
                        {
                            (void)strcat_s(acDescription, sizeof(acDescription), SHC31_NO_IDENTIFY_TXT);
                        }
                    }

                    UINT unIndex = m_unDeviceCnt;

                    sSelHwInterface[unIndex].m_dwIdInterface = unIndex;
                    sSelHwInterface[unIndex].m_acNameInterface = strSerial;
                    sSelHwInterface[unIndex].m_acDescription = acDescription;
                    Shc31WideToNarrow(wstrPath, &(sSelHwInterface[unIndex].m_acDeviceName));

                    psDevice->wstrInterfacePath = wstrPath;
                    psDevice->strDescription = acDescription;
                    m_unDeviceCnt++;

                }
            }

            nCount = (INT)m_unDeviceCnt;
            hResult = (m_unDeviceCnt > 0U) ? S_OK : S_FALSE;

            if (hResult == S_FALSE)
            {
                vSetError("no gs_usb devices enumerated (GUID has no present interfaces)");
            }
        }
        else
        {
            vSetError("CM_Get_Device_Interface_ListW failed");
        }
    }
    else
    {
        vSetError("no gs_usb device interfaces present (CM_Get_Device_Interface_List_SizeW)");
    }

    return hResult;
}

/**
 * \brief         Enumerates the gs_usb device interfaces and shows the
 *                CHardwareListingCAN modal selection dialog so the user can
 *                assign the adapters to logical CAN channels
 *
 * The enumeration and the selection apply run under the driver critical
 * section; the modal dialog itself runs without it. Re-enumeration is
 * refused while any device is open.
 *
 * \param sSelHwInterface  Output interface list
 * \param nCount           Output: number of selected channels
 * \param InitData         Controller details passed to the dialog
 *
 * \return        S_OK for success, S_FALSE when enumeration fails,
 *                HW_INTERFACE_NO_SEL when the dialog is cancelled
 */
HRESULT CDIL_CAN_SHC31::CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    HRESULT hResult = S_FALSE;
    INT anSelList[SHC31_MAX_DEVICES];
    INT nSelCount = 0;
    INT i;
    bool bEnumerated = false;

    for (i = 0; i < (INT)SHC31_MAX_DEVICES; i++)
    {
        anSelList[i] = -1;                       /** -1 = nothing preselected (dialog contract, HardwareListingCAN.cpp:908) */
    }

    EnterCriticalSection(&m_CriticalSection);

    /** Re-enumeration while a device is open would reset the device table
        under the RX thread and CAN_SendMsg, so it is refused. The dialog
        below runs without the critical section for the same reason. */
    bool bAnyOpen = false;

    for (UINT unDevice = 0U; (unDevice < SHC31_MAX_DEVICES) && (bAnyOpen == false); unDevice++)
    {
        if (m_asDevices[unDevice].hWinUsb != nullptr)
        {
            bAnyOpen = true;
        }
    }

    if (bAnyOpen == true)
    {
        vSetError("the hardware list cannot be refreshed while a device is open");
    }
    else if (hEnumerateDevices(sSelHwInterface, nCount) == S_OK)
    {
        bEnumerated = true;

        /** Restore the persisted channel assignment: the dialog preselects
            only from anSelList, so seed it from the project's HardwareDesc
            strings (matched against the stable enumerated description).
            A stale description simply stays in Available until re-added. */
        bool abUsed[SHC31_MAX_DEVICES] = { false };
        INT nPreselect = 0;

        if (InitData != nullptr)
        {
            for (i = 0; (i < nCount) && (nPreselect < (INT)m_unDeviceCnt); i++)
            {
                const std::string& strWanted = InitData[i].m_omHardwareDesc;

                if ((strWanted.empty() == false) && (strWanted != SHC31_SIMULATION_DESC_TXT))
                {
                    for (UINT unDevice = 0U; unDevice < m_unDeviceCnt; unDevice++)
                    {
                        if ((abUsed[unDevice] == false) && (m_asDevices[unDevice].strDescription == strWanted))
                        {
                            anSelList[nPreselect] = (INT)unDevice;
                            nPreselect++;
                            abUsed[unDevice] = true;
                            unDevice = m_unDeviceCnt;
                        }
                    }
                }
            }
        }
    }

    LeaveCriticalSection(&m_CriticalSection);

    /** The modal dialog runs without the critical section: a user interaction
        must not block CAN_SendMsg or the RX thread dispatch, and the device
        table was already reset, so the snapshot shown cannot change until
        the selection is applied below */
    if (bEnumerated == true)
    {
        if (m_hOwnerWnd == nullptr)
        {
            vSetError("no owner window available for the hardware listing dialog");
            hResult = HW_INTERFACE_NO_SEL;
        }
        else
        {
            AFX_MANAGE_STATE(AfxGetStaticModuleState());

            CWnd objMainWnd;
            objMainWnd.Attach(m_hOwnerWnd);
            CShc31ChannelSettings* pAdvancedSettings = new CShc31ChannelSettings(this, InitData, (UINT)nCount);
            CHardwareListingCAN HwList(sSelHwInterface, nCount, anSelList, CAN, CHANNEL_ALLOWED, &objMainWnd, InitData, pAdvancedSettings);
            INT nRet = HwList.DoModal();
            delete pAdvancedSettings;
            objMainWnd.Detach();

            if (nRet == IDOK)
            {
                nSelCount = HwList.nGetSelectedList(anSelList);

                EnterCriticalSection(&m_CriticalSection);

                for (i = 0; i < nSelCount; i++)
                {
                    INT nDevice = anSelList[i];

                    if ((nDevice != i) && (nDevice >= 0) && (nDevice < (INT)m_unDeviceCnt))
                    {
                        Shc31DeviceStruct sSwap = m_asDevices[nDevice];
                        m_asDevices[nDevice] = m_asDevices[i];
                        m_asDevices[i] = sSwap;
                    }
                }

                for (i = 0; i < nSelCount; i++)
                {
                    sSelHwInterface[i].m_dwIdInterface = (unsigned long)i;
                    sSelHwInterface[i].m_acDescription = m_asDevices[i].strDescription;
                    sSelHwInterface[i].m_acNameInterface = m_asDevices[i].strDescription;
                    Shc31WideToNarrow(m_asDevices[i].wstrInterfacePath, &(sSelHwInterface[i].m_acDeviceName));

                    /* Surface the adapter capability to the application. This is a
                       hardware property, so it is never persisted. */
                    if (InitData != nullptr)
                    {
                        InitData[i].m_bSupportCANFD = m_asDevices[i].bFdCapable;
                    }
                }

                nCount = nSelCount;
                m_unSelectedCnt = (UINT)nSelCount;
                vSetState(SHC31_STATE_REGISTERED);
                hResult = S_OK;

                LeaveCriticalSection(&m_CriticalSection);
            }
            else
            {
                hResult = HW_INTERFACE_NO_SEL;
            }
        }
    }

    return hResult;
}

/**
 * \brief         Stores the user-selected hardware interfaces
 *
 * \param sSelHwInterface  Selected interface list
 * \param nCount           Number of selected channels
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount)
{
    HRESULT hResult = S_FALSE;


    EnterCriticalSection(&m_CriticalSection);

    if ((m_eState == SHC31_STATE_REGISTERED) && (nCount > 0) && ((UINT)nCount <= m_unDeviceCnt))
    {
        m_unSelectedCnt = (UINT)nCount;
        vSetState(SHC31_STATE_INITIALISED);
        hResult = S_OK;
    }
    else
    {
        vSetError("improper state or selection for SelectHwInterface");
    }

    LeaveCriticalSection(&m_CriticalSection);

    (void)sSelHwInterface;


    return hResult;
}

/**
 * \brief         Deselects the selected hardware interfaces, closing any
 *                open devices
 *
 * \param         None
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_DeselectHwInterface(void)
{
    HRESULT hResult = S_OK;

    for (UINT i = 0; i < SHC31_MAX_DEVICES; i++)
    {
        (void)hCloseDeviceAndBus(i);
    }

    EnterCriticalSection(&m_CriticalSection);

    if (m_eState == SHC31_STATE_INITIALISED)
    {
        vSetState(SHC31_STATE_REGISTERED);
    }

    LeaveCriticalSection(&m_CriticalSection);

    return hResult;
}

/**
 * \brief         Copies the per-channel controller configuration and
 *                computes the nominal and data-phase bit timing with the
 *                bit-timing calculator. Unusable configuration strings fall
 *                back to the defaults (500k at 87.5 percent / 2M at 75
 *                percent).
 *
 * \param InitData  Controller details array
 * \param Length    Unused array byte length
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    HRESULT hResult = S_OK;
    UINT unConfigured = 0U;

    EnterCriticalSection(&m_CriticalSection);

    for (UINT i = 0; i < SHC31_MAX_DEVICES; i++)
    {
        Shc31DeviceStruct& sDevice = m_asDevices[i];
        const SCONTROLLER_DETAILS& sDetails = InitData[i];

        if (sDevice.wstrInterfacePath.empty() == false)
        {
            uint32_t unNominalRate = (uint32_t)strtoul(sDetails.m_omStrBaudrate.c_str(), nullptr, 10);
            double dNominalSpPercent = strtod(sDetails.m_omStrSamplePercentage.c_str(), nullptr);
            uint32_t unNominalSpBp = (uint32_t)(dNominalSpPercent * 100.0);
            Shc31TimingStruct sTiming;


            sDevice.bListenOnly = (sDetails.m_ucControllerMode == defMODE_PASSIVE) ? true : false;

            if (unNominalRate == 0U)
            {
                unNominalRate = SHC31_DEFAULT_NOMINAL_BITRATE;
            }

            if (unNominalSpBp == 0U)
            {
                unNominalSpBp = SHC31_DEFAULT_NOMINAL_SP_BP;
            }

            if (unNominalSpBp < SHC31_TIMING_SP_BP_MIN)
            {
                unNominalSpBp = SHC31_TIMING_SP_BP_MIN;
            }
            else if (unNominalSpBp > SHC31_TIMING_SP_BP_MAX)
            {
                unNominalSpBp = SHC31_TIMING_SP_BP_MAX;
            }

            if (Shc31TimingCalculate(unNominalRate, unNominalSpBp, SHC31_PHASE_NOMINAL, &sTiming) != SHC31_TIMING_ERR_PARAM)
            {
                sDevice.sNominalTiming = sTiming;
                unConfigured++;
            }
            else
            {
                vSetError("the nominal bitrate configuration is invalid");
                hResult = S_FALSE;
            }

            if (sDetails.m_bcanFDEnabled == TRUE)
            {
                if (sDevice.bFdCapable == false)
                {
                    /** The channel is configured for CAN FD but the adapter
                        does not advertise the FD feature: skip the data phase
                        instead of sending a request the firmware does not
                        implement (it answers with a STALL, which would abort
                        the device start) */
                    vSetError("the CAN FD configuration is ignored: this adapter does not support CAN FD");
                    sDevice.bHasDataTiming = false;
                }
                else
                {
                    uint32_t unDataSpBp = sDetails.m_unDataSamplePoint * 100U;

                    if (unDataSpBp == 0U)
                    {
                        unDataSpBp = SHC31_DEFAULT_DATA_SP_BP;
                    }

                    /** The nominal sample point is clamped into the accepted
                        range; apply the same clamp to the data phase so a
                        stored value outside it falls back instead of failing
                        the whole configuration */
                    if (unDataSpBp < SHC31_TIMING_SP_BP_MIN)
                    {
                        unDataSpBp = SHC31_TIMING_SP_BP_MIN;
                    }
                    else if (unDataSpBp > SHC31_TIMING_SP_BP_MAX)
                    {
                        unDataSpBp = SHC31_TIMING_SP_BP_MAX;
                    }

                    if (Shc31TimingCalculate(sDetails.m_unDataBitRate, unDataSpBp, SHC31_PHASE_DATA, &sTiming) != SHC31_TIMING_ERR_PARAM)
                    {
                        sDevice.sDataTiming = sTiming;
                        sDevice.bHasDataTiming = true;
                    }
                    else
                    {
                        vSetError("the data-phase bitrate configuration is invalid");
                        hResult = S_FALSE;
                    }
                }
            }
            else
            {
                sDevice.bHasDataTiming = false;
            }
        }
    }

    LeaveCriticalSection(&m_CriticalSection);

    (void)Length;

    if (unConfigured == 0U)
    {
        /** No enumerated device received a usable configuration; the start
            path will apply the defaults */
        hResult = S_OK;
    }


    return hResult;
}

/**
 * \brief         Writes the channel configuration to the device: host byte
 *                order, nominal (and data-phase) bit timing, then MODE
 *                RESET followed by MODE START with the channel flags
 *                (bittiming is only accepted while the channel is closed)
 *
 * \param unDeviceIndex  Index into the device table
 *
 * \return        S_OK on success, S_FALSE on failure
 */
HRESULT CDIL_CAN_SHC31::hConfigureDevice(UINT unDeviceIndex)
{
    HRESULT hResult = S_FALSE;
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);
    uint8_t aunPayload[SHC31_GSUSB_BITTIMING_SIZE] = { 0U };
    const Shc31TimingStruct* psTiming = &(psDevice->sNominalTiming);
    bool bTimingWritten = false;

    /** HOST_FORMAT: announce the host's little-endian u32 wire order */
    Shc31GsUsbPutU32(aunPayload, SHC31_GSUSB_HOST_FORMAT_MAGIC);

    if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_HOST_FORMAT, 0U, aunPayload, 4U) == false)
    {
        vSetError("the HOST_FORMAT request failed");
    }
    else
    {
        /** Nominal bit timing: 5-field gs_device_bittiming */
        Shc31GsUsbPutU32(aunPayload + 0, psTiming->sWire.PropSeg);
        Shc31GsUsbPutU32(aunPayload + 4, psTiming->sWire.PhaseSeg1);
        Shc31GsUsbPutU32(aunPayload + 8, psTiming->sWire.PhaseSeg2);
        Shc31GsUsbPutU32(aunPayload + 12, psTiming->sWire.Sjw);
        Shc31GsUsbPutU32(aunPayload + 16, psTiming->sWire.Brp);

        if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_BITTIMING, 0U, aunPayload, SHC31_GSUSB_BITTIMING_SIZE) == false)
        {
            vSetError("the BITTIMING request failed");
        }
        else if (psDevice->bHasDataTiming == true)
        {
            psTiming = &(psDevice->sDataTiming);

            Shc31GsUsbPutU32(aunPayload + 0, psTiming->sWire.PropSeg);
            Shc31GsUsbPutU32(aunPayload + 4, psTiming->sWire.PhaseSeg1);
            Shc31GsUsbPutU32(aunPayload + 8, psTiming->sWire.PhaseSeg2);
            Shc31GsUsbPutU32(aunPayload + 12, psTiming->sWire.Sjw);
            Shc31GsUsbPutU32(aunPayload + 16, psTiming->sWire.Brp);

            if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_DATA_BITTIMING, 0U, aunPayload, SHC31_GSUSB_BITTIMING_SIZE) == false)
            {
                vSetError("the DATA_BITTIMING request failed");
            }
            else
            {
                bTimingWritten = true;
            }
        }
        else
        {
            bTimingWritten = true;
        }
    }

    if (bTimingWritten == true)
    {
        /** No PAD_PKTS flag: the gs_usb firmware contract prescribes
            variable-length, short-packet-terminated transfers so every read
            completes exactly at an event boundary and the RX parse cannot
            lose sync */
        uint32_t unModeFlags = SHC31_GSUSB_MODE_FLAG_HW_TIMESTAMP;


        if ((psDevice->bFdCapable == true) && (psDevice->bHasDataTiming == true))
        {
            unModeFlags |= SHC31_GSUSB_MODE_FLAG_FD;
        }

        if (psDevice->bListenOnly == true)
        {
            unModeFlags |= SHC31_GSUSB_MODE_FLAG_LISTEN_ONLY;
        }

        /** MODE RESET purges pending frames (v1.5), then MODE START */
        Shc31GsUsbPutU32(aunPayload + 0, SHC31_GSUSB_MODE_RESET);
        Shc31GsUsbPutU32(aunPayload + 4, 0U);

        if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_MODE, 0U, aunPayload, SHC31_GSUSB_MODE_SIZE) == false)
        {
            vSetError("the MODE RESET request failed");
        }
        else
        {
            Shc31GsUsbPutU32(aunPayload + 0, SHC31_GSUSB_MODE_START);
            Shc31GsUsbPutU32(aunPayload + 4, unModeFlags);

            if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_MODE, 0U, aunPayload, SHC31_GSUSB_MODE_SIZE) == false)
            {
                vSetError("the MODE START request failed");
            }
            else
            {
                hResult = S_OK;
            }
        }
    }

    return hResult;
}

/**
 * \brief         Opens the WinUSB device, applies the channel
 *                configuration and spawns the RX thread
 *
 * \param unDeviceIndex  Index into the device table
 *
 * \return        S_OK on success, S_FALSE on failure
 */
HRESULT CDIL_CAN_SHC31::hOpenDeviceAndBus(UINT unDeviceIndex)
{
    HRESULT hResult = S_FALSE;
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);
    UCHAR ucPipeIn = 0U;
    UCHAR ucPipeOut = 0U;


    if (psDevice->hWinUsb != nullptr)
    {
        /** Normally the shutdown closed this entry first. A remaining handle
            means the RX thread of the previous session did not exit within
            the join timeout, so its handles stay allocated; overwriting them
            would hand a live thread's state over to a new session. */
        psDevice->bFailed = true;
        vSetError("the channel cannot be opened: the device was never closed or its RX thread is still running");
    }
    else if (Shc31WinUsbOpen(psDevice->wstrInterfacePath.c_str(), &(psDevice->hDevice), &(psDevice->hWinUsb), &ucPipeIn, &ucPipeOut) == true)
    {
        psDevice->ucPipeIn = ucPipeIn;
        psDevice->ucPipeOut = ucPipeOut;

        hResult = hConfigureDevice(unDeviceIndex);

        if (hResult == S_OK)
        {
            /** The firmware keeps frames received before this session in its
                host TX queue, so a connect can otherwise show old traffic
                (observed with a 0x020 frame stamped ~40 minutes earlier).
                Drain the IN pipe before the RX thread starts; aborting the
                pending read on timeout clears the endpoint and drops the
                stale queue. */
            for (UINT unDrain = 0U; unDrain < SHC31_RX_DRAIN_MAX_FRAMES; unDrain++)
            {
                OVERLAPPED sDrain;
                ULONG dwRead = 0UL;
                BOOL bDrained = FALSE;

                memset(&sDrain, 0, sizeof(sDrain));
                sDrain.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

                if (sDrain.hEvent == nullptr)
                {
                    break;
                }

                bDrained = WinUsb_ReadPipe(psDevice->hWinUsb, psDevice->ucPipeIn,
                                           psDevice->aunRxBuffer, SHC31_GSUSB_RX_XFER_SIZE,
                                           &dwRead, &sDrain);

                if ((bDrained == FALSE) && (GetLastError() == ERROR_IO_PENDING))
                {
                    if (WaitForSingleObject(sDrain.hEvent, SHC31_RX_DRAIN_TIMEOUT_MS) == WAIT_OBJECT_0)
                    {
                        bDrained = WinUsb_GetOverlappedResult(psDevice->hWinUsb, &sDrain, &dwRead, FALSE);
                    }
                    else
                    {
                        (void)WinUsb_AbortPipe(psDevice->hWinUsb, psDevice->ucPipeIn);
                        bDrained = FALSE;
                    }
                }

                (void)CloseHandle(sDrain.hEvent);

                if ((bDrained == FALSE) || (dwRead == 0UL))
                {
                    break;
                }
            }

            /** A shutdown that hit the join timeout can have left the exit
                event of a previous session allocated; free it first so the
                handle is not overwritten and leaked */
            if (psDevice->hRxExitEvent != nullptr)
            {
                (void)CloseHandle(psDevice->hRxExitEvent);
                psDevice->hRxExitEvent = nullptr;
            }

            psDevice->hRxExitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            psDevice->bBusOpen = true;

            Shc31RxThreadStruct* psThread = new Shc31RxThreadStruct;
            psThread->pDriver = this;
            psThread->unDeviceIndex = unDeviceIndex;

            HANDLE hThread = CreateThread(nullptr, 0, Shc31RxThreadProc, psThread, 0, nullptr);
            psDevice->hRxThread = hThread;

            if (hThread == nullptr)
            {
                delete psThread;
                psDevice->bBusOpen = false;
                vSetError("the RX thread could not be created");

                if (psDevice->hRxExitEvent != nullptr)
                {
                    (void)CloseHandle(psDevice->hRxExitEvent);
                    psDevice->hRxExitEvent = nullptr;
                }

                Shc31WinUsbClose(&(psDevice->hDevice), &(psDevice->hWinUsb));
                hResult = S_FALSE;
            }
        }
        else
        {
            Shc31WinUsbClose(&(psDevice->hDevice), &(psDevice->hWinUsb));
        }
    }
    else
    {
        vSetError("the device interface could not be opened through WinUSB");
        hResult = S_FALSE;
    }

    return hResult;
}

/**
 * \brief         Closes the bus (MODE RESET), stops the RX thread and
 *                closes the WinUSB device. Shutdown order: MODE reset,
 *                exit event, abort pipe, thread join, handle close.
 *
 * The table update and the handle freeing run under the driver critical
 * section, so a concurrent CAN_SendMsg cannot write to the handle that is
 * freed here; MODE RESET and the thread join run outside the lock so a
 * wedged device or a slow RX exit cannot block the other driver calls.
 *
 * \param unDeviceIndex  Index into the device table
 *
 * \return        S_OK on success, S_FALSE when the RX thread did not exit
 */
HRESULT CDIL_CAN_SHC31::hCloseDeviceAndBus(UINT unDeviceIndex)
{
    HRESULT hResult = S_OK;
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);
    HANDLE hThread = nullptr;
    bool bJoinAttempted = false;
    bool bThreadJoined = false;

    /** MODE RESET first and outside the critical section: it is best effort
        and a control transfer to a wedged device must not block CAN_SendMsg */
    if (psDevice->bBusOpen == true)
    {
        uint8_t aunPayload[SHC31_GSUSB_MODE_SIZE] = { 0U };

        Shc31GsUsbPutU32(aunPayload + 0, SHC31_GSUSB_MODE_RESET);
        Shc31GsUsbPutU32(aunPayload + 4, 0U);

        if (Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_MODE, 0U, aunPayload, SHC31_GSUSB_MODE_SIZE) == false)
        {
            /** Best effort during shutdown; the device may already be gone */
        }
    }

    EnterCriticalSection(&m_CriticalSection);

    if (psDevice->hWinUsb != nullptr)
    {
        /** Detach the thread from the shared entry before the join: the join
            must not run under the critical section, because the RX thread
            may be inside vSetError at this moment and needs the same lock */
        hThread = psDevice->hRxThread;
        psDevice->hRxThread = nullptr;

        if (psDevice->hRxExitEvent != nullptr)
        {
            (void)SetEvent(psDevice->hRxExitEvent);
        }

        if (psDevice->sRxOverlapped.hEvent != nullptr)
        {
            (void)WinUsb_AbortPipe(psDevice->hWinUsb, psDevice->ucPipeIn);
        }

        psDevice->bBusOpen = false;
    }

    LeaveCriticalSection(&m_CriticalSection);

    if (hThread != nullptr)
    {
        bJoinAttempted = true;
        bThreadJoined = (WaitForSingleObject(hThread, SHC31_RX_JOIN_TIMEOUT_MS) == WAIT_OBJECT_0);

        (void)CloseHandle(hThread);
    }

    EnterCriticalSection(&m_CriticalSection);

    /** Without a join attempt the entry was either never opened with an RX
        thread, orphaned by an earlier join timeout, or an open is in flight;
        its handles must stay untouched so a live thread cannot lose them */
    if (bJoinAttempted == true)
    {
        if (bThreadJoined == false)
        {
            /** The RX thread did not exit within the join timeout. Its
                OVERLAPPED and the device handles may still be in use by that
                thread; freeing them here would crash later. The leak is
                contained by leaving the entry failed and closed, and
                CAN_ListHwInterfaces refuses to re-enumerate while a device
                handle exists. */
            psDevice->bFailed = true;
            vSetError("the RX thread did not exit during shutdown");
            hResult = S_FALSE;
        }
        else if (psDevice->hWinUsb != nullptr)
        {
            if (psDevice->hRxExitEvent != nullptr)
            {
                (void)CloseHandle(psDevice->hRxExitEvent);
                psDevice->hRxExitEvent = nullptr;
            }

            if ((m_unIdentifyChannel == (unDeviceIndex + 1U)) && (psDevice->bIdentifyCapable == true))
            {
                uint8_t aunOff[SHC31_GSUSB_IDENTIFY_SIZE] = { 0U };

                Shc31GsUsbPutU32(aunOff, 0U);
                (void)Shc31WinUsbControlOut(psDevice->hWinUsb, SHC31_GSUSB_BREQ_IDENTIFY, 0U, aunOff, SHC31_GSUSB_IDENTIFY_SIZE);
                m_unIdentifyChannel = 0U;
            }

            Shc31WinUsbClose(&(psDevice->hDevice), &(psDevice->hWinUsb));

            for (UINT i = 0; i < SHC31_GSUSB_ECHO_POOL_MAX; i++)
            {
                psDevice->abEchoInFlight[i] = false;
            }
        }
    }

    psDevice->bBusOpen = false;

    if (m_unIdentifyChannel == (unDeviceIndex + 1U))
    {
        m_unIdentifyChannel = 0;
    }

    LeaveCriticalSection(&m_CriticalSection);

    return hResult;
}

/**
 * \brief         Sends one gs_usb IDENTIFY request to a device, opening
 *                WinUSB temporarily when the channel is closed. The firmware
 *                keeps the blink state until it receives IDENTIFY-off, so the
 *                temporary open/close leaves the requested state active.
 *
 * \param unDeviceIndex  Device index
 * \param bOn            TRUE to start blinking, FALSE to stop
 *
 * \return        true when the control transfer succeeded
 */
bool CDIL_CAN_SHC31::bSendIdentifyRequest(UINT unDeviceIndex, bool bOn)
{
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);
    uint8_t aunPayload[SHC31_GSUSB_IDENTIFY_SIZE] = { 0U };
    bool bSuccess = false;
    HANDLE hDevice = nullptr;
    WINUSB_INTERFACE_HANDLE hWinUsb = nullptr;
    UCHAR ucPipeIn = 0U;
    UCHAR ucPipeOut = 0U;
    WINUSB_INTERFACE_HANDLE hTarget = psDevice->hWinUsb;

    if (hTarget == nullptr)
    {
        if (Shc31WinUsbOpen(psDevice->wstrInterfacePath.c_str(), &hDevice, &hWinUsb, &ucPipeIn, &ucPipeOut) == true)
        {
            hTarget = hWinUsb;
        }
    }

    if (hTarget != nullptr)
    {
        Shc31GsUsbPutU32(aunPayload, (bOn == true) ? 1U : 0U);
        bSuccess = Shc31WinUsbControlOut(hTarget, SHC31_GSUSB_BREQ_IDENTIFY, 0U, aunPayload, SHC31_GSUSB_IDENTIFY_SIZE);
    }

    if ((hDevice != nullptr) || (hWinUsb != nullptr))
    {
        Shc31WinUsbClose(&hDevice, &hWinUsb);
    }

    return bSuccess;
}

/**
 * \brief         Sends the gs_usb IDENTIFY request to one device with the
 *                max-one-at-a-time policy: a request for channel N while
 *                channel M identifies sends IDENTIFY-off to M first
 *                (temporarily opening M when it is closed). Rejected for
 *                devices without the identify feature bit.
 *
 * \param unDeviceIndex  Device index
 * \param bOn            TRUE to start blinking, FALSE to stop
 *
 * \return        S_OK on success, S_FALSE on failure
 */
HRESULT CDIL_CAN_SHC31::hSendIdentify(UINT unDeviceIndex, bool bOn)
{
    HRESULT hResult = S_FALSE;
    Shc31DeviceStruct* psDevice = &(m_asDevices[unDeviceIndex]);

    if (psDevice->bIdentifyCapable == false)
    {
        vSetError("this device does not advertise the gs_usb IDENTIFY feature");
    }
    else
    {
        if ((bOn == true) && (m_unIdentifyChannel != 0U) && (m_unIdentifyChannel != (unDeviceIndex + 1U)))
        {
            UINT unPrevious = m_unIdentifyChannel - 1U;

            if ((unPrevious < m_unDeviceCnt) && (m_asDevices[unPrevious].bIdentifyCapable == true))
            {
                (void)bSendIdentifyRequest(unPrevious, false);
            }
        }

        if (bSendIdentifyRequest(unDeviceIndex, bOn) == true)
        {
            if (bOn == true)
            {
                m_unIdentifyChannel = unDeviceIndex + 1U;
            }
            else if (m_unIdentifyChannel == (unDeviceIndex + 1U))
            {
                m_unIdentifyChannel = 0U;
            }
            else
            {
                /** An off request for a device that is not identified leaves
                    the saved channel untouched */
            }

            hResult = S_OK;
        }
        else
        {
            vSetError("the IDENTIFY request failed");
        }
    }

    return hResult;
}

/**
 * \brief         Resolves an adapter by its persistence description and
 *                returns the FD / identify capability flags (dialog accessor)
 *
 * \param strDescription     Value matched against the device description
 * \param pbFdCapable        Output: GS_CAN_FEATURE_FD advertised
 * \param pbIdentifyCapable  Output: GS_CAN_FEATURE_IDENTIFY advertised
 *
 * \return        true when a matching device was found
 */
bool CDIL_CAN_SHC31::bGetDeviceInfo(const std::string& strDescription, bool* pbFdCapable, bool* pbIdentifyCapable)
{
    bool bFound = false;

    if (pbFdCapable != nullptr)
    {
        *pbFdCapable = false;
    }

    if (pbIdentifyCapable != nullptr)
    {
        *pbIdentifyCapable = false;
    }

    if (strDescription.empty() == false)
    {
        for (UINT i = 0; i < m_unDeviceCnt; i++)
        {
            if (m_asDevices[i].strDescription == strDescription)
            {
                if (pbFdCapable != nullptr)
                {
                    *pbFdCapable = m_asDevices[i].bFdCapable;
                }

                if (pbIdentifyCapable != nullptr)
                {
                    *pbIdentifyCapable = m_asDevices[i].bIdentifyCapable;
                }

                bFound = true;
                i = m_unDeviceCnt;
            }
        }
    }

    return bFound;
}

/**
 * \brief         Starts or stops the identify blink on the adapter named by
 *                its persistence description (dialog accessor)
 *
 * \param strDescription  Value matched against the device description
 * \param bOn             TRUE to start blinking, FALSE to stop
 *
 * \return        S_OK on success, S_FALSE when no device matches or the
 *                request fails
 */
HRESULT CDIL_CAN_SHC31::hSendIdentifyByDescription(const std::string& strDescription, bool bOn)
{
    HRESULT hResult = S_FALSE;
    bool bFound = false;

    if (strDescription.empty() == false)
    {
        for (UINT i = 0; i < m_unDeviceCnt; i++)
        {
            if (m_asDevices[i].strDescription == strDescription)
            {
                hResult = hSendIdentify(i, bOn);
                bFound = true;
                i = m_unDeviceCnt;
            }
        }
    }

    if (bFound == false)
    {
        vSetError("the identify request did not match an enumerated device");
    }

    return hResult;
}

/**
 * \brief         Free-function accessor used by the settings dialog: resolves
 *                a device description to its FD / identify capability flags
 *
 * \param pDriver            Driver instance
 * \param strDescription     Device description to resolve
 * \param pbFdCapable        Output: FD feature advertised
 * \param pbIdentifyCapable  Output: identify feature advertised
 *
 * \return        true when the device was found
 */
bool Shc31GetDeviceInfo(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool* pbFdCapable, bool* pbIdentifyCapable)
{
    bool bFound = false;

    if (pDriver != nullptr)
    {
        bFound = pDriver->bGetDeviceInfo(strDescription, pbFdCapable, pbIdentifyCapable);
    }

    return bFound;
}

/**
 * \brief         Free-function accessor used by the settings dialog: starts or
 *                stops the identify blink for a device description
 *
 * \param pDriver         Driver instance
 * \param strDescription  Device description to resolve
 * \param bOn             TRUE to start blinking, FALSE to stop
 *
 * \return        S_OK on success, S_FALSE otherwise
 */
HRESULT Shc31SendIdentifyByDescription(CDIL_CAN_SHC31* pDriver, const std::string& strDescription, bool bOn)
{
    HRESULT hResult = S_FALSE;

    if (pDriver != nullptr)
    {
        hResult = pDriver->hSendIdentifyByDescription(strDescription, bOn);
    }

    return hResult;
}

/**
 * \brief         Starts the hardware: opens and configures every selected
 *                device, applies the channel configuration and spawns the
 *                RX threads
 *
 * \param         None
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_StartHardware(void)
{
    HRESULT hResult = S_FALSE;
    UINT unOpened = 0U;

    if (m_eState == SHC31_STATE_INITIALISED)
    {
        for (UINT i = 0; i < SHC31_MAX_DEVICES; i++)
        {
            if (m_asDevices[i].wstrInterfacePath.empty() == false)
            {
                if (hOpenDeviceAndBus(i) == S_OK)
                {
                    unOpened++;
                }
            }
        }

        if (unOpened > 0U)
        {
            vSetState(SHC31_STATE_CONNECTED);
            hResult = S_OK;
        }
        else
        {
            vSetError("no device could be opened");
        }

    }
    else
    {
        vSetError("improper state for StartHardware");
    }

    return hResult;
}

/**
 * \brief         Stops the hardware: closes the bus and every open device
 *
 * \param         None
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_StopHardware(void)
{
    HRESULT hResult = S_OK;

    for (UINT i = 0; i < SHC31_MAX_DEVICES; i++)
    {
        (void)hCloseDeviceAndBus(i);
    }

    if (m_eState == SHC31_STATE_CONNECTED)
    {
        vSetState(SHC31_STATE_INITIALISED);
    }

    return hResult;
}

/**
 * \brief         Gets the controller status
 *
 * \param StatusData  Output status structure
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    HRESULT hResult = S_OK;

    if (m_eState == SHC31_STATE_CONNECTED)
    {
        StatusData.wControllerStatus = NORMAL_ACTIVE;
    }
    else
    {
        StatusData.wControllerStatus = INITIALISED;
    }

    return hResult;
}

/**
 * \brief         Encodes and transmits one message on the message's
 *                channel. The message is written to the device of the
 *                logical channel (m_ucChannel, 1-based) as a full
 *                gs_host_frame with an echo-pool slot (max 30 in flight
 *                per device); the echo frame received
 *                from the device releases the slot and is delivered to the
 *                clients with the TX flag.
 *
 * \param dwClientID   Registered client identifier
 * \param sCanTxMsg    Message to send
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    HRESULT hResult = S_FALSE;
    UINT unClientIndex = 0U;
    BOOL bClientKnown = bGetClientObj(dwClientID, unClientIndex);
    BOOL bChannelValid = ((sCanTxMsg.m_ucChannel >= 1U) && (sCanTxMsg.m_ucChannel <= SHC31_MAX_DEVICES));
    UINT unDeviceIndex = 0U;
    BOOL bDeviceReady = FALSE;
    Shc31DeviceStruct* psDevice = nullptr;
    uint8_t aunFrame[SHC31_GSUSB_FRAME_MAX_SIZE];
    uint32_t unFrameLen = 0U;

    if (bChannelValid == TRUE)
    {
        unDeviceIndex = (UINT)sCanTxMsg.m_ucChannel - 1U;
        psDevice = &(m_asDevices[unDeviceIndex]);

        /** Preliminary check; the authoritative re-check runs below under the
            critical section, because hCloseDeviceAndBus frees the device
            handles under the same lock */
        if ((psDevice->hWinUsb != nullptr) && (psDevice->bBusOpen == true) && (psDevice->bFailed == false))
        {
            bDeviceReady = TRUE;
        }
    }

    if ((bClientKnown == TRUE) && (bDeviceReady == TRUE))
    {
        bool bFd = (sCanTxMsg.m_bCANFD == TRUE);
        /** BUSMASTER-NG does not expose the gs_usb BRS flag in STCAN_MSG, so
            FD frames are transmitted without bit-rate switching */
        bool bBrs = false;
        bool bRtr = (sCanTxMsg.m_ucRTR != 0U);
        UINT unSlot = 0U;


        EnterCriticalSection(&m_CriticalSection);

        if ((psDevice->hWinUsb != nullptr) && (psDevice->bBusOpen == true) && (psDevice->bFailed == false))
        {
            if (hAcquireEchoSlot(psDevice, &unSlot) == S_OK)
            {
                unFrameLen = Shc31GsUsbEncodeTxFrame(sCanTxMsg.m_unMsgID, sCanTxMsg.m_ucEXTENDED != 0U, bRtr, bFd, bBrs,
                                                     sCanTxMsg.m_ucDataLen, sCanTxMsg.m_ucData,
                                                     unSlot, aunFrame, sizeof(aunFrame));

                if (unFrameLen == 0U)
                {
                    vReleaseEchoSlot(psDevice, unSlot);
                    vSetError("the message cannot be encoded as a gs_host_frame");
                }
                else if (Shc31WinUsbWritePipe(psDevice->hWinUsb, psDevice->ucPipeOut, aunFrame, unFrameLen) == true)
                {
                    hResult = S_OK;
                }
                else
                {
                    vReleaseEchoSlot(psDevice, unSlot);
                    vSetError("the bulk OUT frame write failed");
                }
            }
            else
            {
                vSetError("the TX echo pool is exhausted (30 frames in flight per device)");
            }
        }
        else
        {
            vSetError("transmit rejected: the channel was closed on another thread");
        }

        LeaveCriticalSection(&m_CriticalSection);
    }
    else
    {
        vSetError("transmit rejected: unknown client, closed device or invalid channel");
    }

    return hResult;
}

/**
 * \brief         Returns the last driver error string
 *
 * \param acErrorStr  Output error string
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_GetLastErrorString(std::string& acErrorStr)
{
    /** The error string is concurrently written by the RX threads, so the
        read gets the same guard as vSetError */
    EnterCriticalSection(&m_CriticalSection);
    acErrorStr = m_strError;
    LeaveCriticalSection(&m_CriticalSection);

    return S_OK;
}

/**
 * \brief         Gets a controller parameter
 *
 * \param lParam        Output parameter value
 * \param nChannel      Logical channel number, 1-based
 * \param eContrParam   Parameter selector
 *
 * \return        S_OK for success, S_FALSE for unsupported parameters
 */
HRESULT CDIL_CAN_SHC31::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    HRESULT hResult = S_OK;

    switch (eContrParam)
    {
        case NUMBER_HW:
        {
            lParam = (LONG)m_unDeviceCnt;
        }
        break;

        case NUMBER_CONNECTED_HW:
        {
            UINT unOpened = 0U;

            for (UINT i = 0; i < SHC31_MAX_DEVICES; i++)
            {
                if (m_asDevices[i].hWinUsb != nullptr)
                {
                    unOpened++;
                }
            }
            lParam = (LONG)unOpened;
        }
        break;

        case DRIVER_STATUS:
        {
            lParam = (m_eState == SHC31_STATE_CONNECTED) ? TRUE : FALSE;
        }
        break;

        case HW_MODE:
        {
            lParam = defMODE_ACTIVE;
        }
        break;

        case CON_TEST:
        {
            lParam = (LONG)-1;
        }
        break;

        default:
        {
            hResult = S_FALSE;
        }
        break;
    }

    (void)nChannel;

    return hResult;
}

/**
 * \brief         Sets a controller parameter. Listen-only mode selection is
 *                applied on the next hardware start; this entry point
 *                accepts and stores nothing further.
 *
 * \param nValue        Parameter value
 * \param eContrparam   Parameter selector
 *
 * \return        S_OK for success, S_FALSE for unsupported parameters
 */
HRESULT CDIL_CAN_SHC31::CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam)
{
    (void)nValue;
    (void)eContrparam;
    return S_OK;
}

/**
 * \brief         Gets the error counters. Not implemented yet; the
 *                GS_USB_BREQ_GET_STATE mapping is absent, so zeros are
 *                reported.
 *
 * \param sErrorCnt     Output error counters
 * \param nChannel      Logical channel number, 1-based
 * \param eContrParam   Parameter selector
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam)
{
    (void)nChannel;
    (void)eContrParam;
    memset(&sErrorCnt, 0, sizeof(SERROR_CNT));
    return S_OK;
}

/**
 * \brief         Stores the owner window handle
 *
 * \param hWndOwner  Owner window handle
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_SetAppParams(HWND hWndOwner)
{
    HRESULT hResult = S_OK;

    if (m_eState == SHC31_STATE_PRIMORDIAL)
    {
        m_hOwnerWnd = hWndOwner;
        vSetState(SHC31_STATE_RESET);
    }
    else
    {
        vSetError("improper state for SetAppParams");
        hResult = S_FALSE;
    }

    return hResult;
}

/**
 * \brief         Manages a client message buffer
 *
 * \param bAction   MSGBUF_ADD adds pBufObj, MSGBUF_CLEAR removes it (or all
 *                  when pBufObj is null)
 * \param ClientID  Client identifier
 * \param pBufObj   Buffer object
 *
 * \return        S_OK for success, S_FALSE or an error code on failure
 */
HRESULT CDIL_CAN_SHC31::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    HRESULT hResult = S_FALSE;

    if (ClientID != 0U)
    {
        UINT unClientIndex = 0U;

        if (bGetClientObj(ClientID, unClientIndex) == TRUE)
        {
            Shc31ClientStruct& sClientObj = m_asClients[unClientIndex];

            if (bAction == MSGBUF_ADD)
            {
                if (pBufObj != nullptr)
                {
                    if (sClientObj.unBufCount < SHC31_MAX_BUFFERS)
                    {
                        if (bIsBufferExists(sClientObj, pBufObj) == FALSE)
                        {
                            sClientObj.pClientBuf[sClientObj.unBufCount] = pBufObj;
                            sClientObj.unBufCount++;
                            hResult = S_OK;
                        }
                        else
                        {
                            hResult = ERR_BUFFER_EXISTS;
                        }
                    }
                }
            }
            else if (bAction == MSGBUF_CLEAR)
            {
                if (pBufObj != nullptr)
                {
                    (void)bRemoveClientBuffer(sClientObj.pClientBuf, sClientObj.unBufCount, pBufObj);
                }
                else
                {
                    for (UINT i = 0; i < sClientObj.unBufCount; i++)
                    {
                        sClientObj.pClientBuf[i] = nullptr;
                    }
                    sClientObj.unBufCount = 0;
                }
                hResult = S_OK;
            }
            else
            {
                hResult = S_FALSE;
            }
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    else
    {
        if (bAction == MSGBUF_CLEAR)
        {
            for (UINT i = 0; i < m_unClientCnt; i++)
            {
                (void)CAN_ManageMsgBuf(MSGBUF_CLEAR, m_asClients[i].dwClientID, nullptr);
            }
            hResult = S_OK;
        }
    }

    return hResult;
}

/**
 * \brief         Registers or unregisters a client
 *
 * \param bRegister      TRUE to register, FALSE to unregister
 * \param ClientID       Client identifier, updated on registration
 * \param pacClientName  Client name
 *
 * \return        S_OK for success, ERR_CLIENT_EXISTS, ERR_NO_CLIENT_EXIST or
 *                ERR_NO_MORE_CLIENT_ALLOWED on failure
 */
HRESULT CDIL_CAN_SHC31::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    HRESULT hResult = S_FALSE;

    if (bRegister == TRUE)
    {
        if (m_unClientCnt < SHC31_MAX_CLIENTS)
        {
            INT Index = 0;

            if (bClientExist(pacClientName, Index) == FALSE)
            {
                DWORD dwClientId = (DWORD)unGetAvailableClientId();

                if (dwClientId != 0U)
                {
                    UINT unSlot = m_unClientCnt;

                    m_asClients[unSlot].dwClientID = dwClientId;
                    m_asClients[unSlot].strClientName = pacClientName;
                    m_asClients[unSlot].unBufCount = 0;
                    ClientID = dwClientId;
                    m_unClientCnt++;
                    hResult = S_OK;
                }
                else
                {
                    /** Cannot happen with the count check above, kept for a
                        deterministic behaviour if the invariant ever breaks */
                    hResult = ERR_NO_MORE_CLIENT_ALLOWED;
                }
            }
            else
            {
                ClientID = m_asClients[Index].dwClientID;
                hResult = ERR_CLIENT_EXISTS;
            }
        }
        else
        {
            hResult = ERR_NO_MORE_CLIENT_ALLOWED;
        }
    }
    else
    {
        if (bRemoveClient(ClientID) == TRUE)
        {
            hResult = S_OK;
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }

    return hResult;
}

/**
 * \brief         Gets the controller status; the registered event is kept
 *                for status-change signalling
 *
 * \param hEvent          Status change event, ignored for now
 * \param unCntrlStatus   Output controller status
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus)
{
    HRESULT hResult = S_OK;

    switch (m_eState)
    {
        case SHC31_STATE_CONNECTED:
        {
            unCntrlStatus = NORMAL_ACTIVE;
        }
        break;

        case SHC31_STATE_INITIALISED:
        {
            unCntrlStatus = INITIALISED;
        }
        break;

        case SHC31_STATE_REGISTERED:
        {
            unCntrlStatus = RESET_STATE;
        }
        break;

        case SHC31_STATE_PRIMORDIAL:
        case SHC31_STATE_RESET:
        default:
        {
            hResult = S_FALSE;
        }
        break;
    }

    (void)hEvent;

    return hResult;
}

/**
 * \brief         Loads vendor driver libraries: none are required, the
 *                driver links the in-box WinUSB API directly
 *
 * \param         None
 *
 * \return        S_OK always
 */
HRESULT CDIL_CAN_SHC31::CAN_LoadDriverLibrary(void)
{
    return S_OK;
}

/**
 * \brief         Unloads vendor driver libraries: none were loaded
 *
 * \param         None
 *
 * \return        S_OK always
 */
HRESULT CDIL_CAN_SHC31::CAN_UnloadDriverLibrary(void)
{
    return S_OK;
}

/**
 * \brief         Applies the channel-to-device mapping persisted by
 *                description string (m_omHardwareDesc). Devices whose
 *                description matches an entry are treated as the selected
 *                channels for that logical channel index.
 *
 * \param psControllerDetails  Controller details array
 * \param dwDriverId           Driver identifier, ignored
 * \param bIsHardwareListed    TRUE when the hardware list is current
 * \param unChannelCnt         Number of configured channels
 *
 * \return        S_OK for success, S_FALSE for failure
 */
HRESULT CDIL_CAN_SHC31::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS psControllerDetails, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCnt)
{
    HRESULT hResult = S_OK;
    UINT unMatched = 0U;

    for (UINT unChannel = 0; (unChannel < unChannelCnt) && (unChannel < SHC31_MAX_DEVICES); unChannel++)
    {
        const std::string& strWanted = psControllerDetails[unChannel].m_omHardwareDesc;

        if ((strWanted.empty() == false) && (strWanted != SHC31_SIMULATION_DESC_TXT))
        {

            for (UINT unDevice = 0; unDevice < m_unDeviceCnt; unDevice++)
            {
                if (m_asDevices[unDevice].strDescription == strWanted)
                {
                    if (unDevice != unChannel)
                    {
                        Shc31DeviceStruct sSwap = m_asDevices[unDevice];
                        m_asDevices[unDevice] = m_asDevices[unChannel];
                        m_asDevices[unChannel] = sSwap;
                    }
                    unMatched++;
                    unDevice = m_unDeviceCnt;
                }
            }
        }
    }

    m_unSelectedCnt = unMatched;

    if ((bIsHardwareListed == true) && (unMatched == 0U))
    {
        vSetError("no configured channel description matched an enumerated device");
        hResult = S_FALSE;
    }

    (void)dwDriverId;


    return hResult;
}

