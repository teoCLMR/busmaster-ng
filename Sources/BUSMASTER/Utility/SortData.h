
#include "IBMNetWorkService.h"
#include "CANDriverDefines.h"
#include "..\Application\Hashdefines.h"
#include <algorithm>
#include <functional>
using namespace std;

enum EColumnType
{
    eNoColType = 0,
    eTimeStampCol,
    eTxRxCol,
    eChannelCol,
    eMsgTypeCol,
    eCANIDCol,
    eMsgNameCol,
    eMsgDLCCol,
    eFrequencyCol
};

template <typename SBUFFERTYPE>
class CCANMsgWndDataHandler
{
private:
    static EColumnType  m_nSortField;
    static int  m_nMFactor;
    static IBMNetWorkGetService * m_pNetworkService;

    static void SetColumnField ( int nField )
    {
        switch ( nField )
        {
            case 1:
                m_nSortField = EColumnType::eTimeStampCol;
                break;
            case 2:
                m_nSortField = EColumnType::eTxRxCol;
                break;
            case 3:
                m_nSortField = EColumnType::eChannelCol;
                break;
            case 4:
                m_nSortField = EColumnType::eMsgTypeCol;
                break;
            case 5:
                m_nSortField = EColumnType::eCANIDCol;
                break;
            case 6:
                m_nSortField = EColumnType::eMsgNameCol;
                break;
            case 7:
                m_nSortField = EColumnType::eMsgDLCCol;
                break;
            case 9: // header item 9: the Frequency column (8 is Data Byte(s), not sortable)
                m_nSortField = EColumnType::eFrequencyCol;
                break;
            default:
                m_nSortField = EColumnType::eNoColType;
                break;
        }
    }
    

public:
    CCANMsgWndDataHandler ()
    {
        this->m_nSortField = EColumnType::eNoColType;
        m_nMFactor = -1;
        m_pNetworkService = nullptr;
    }
    // BUSMASTER-NG: gives the frequency of an entry, for sorting by the Frequency column (set by the caller of SortBufferData)
    static std::function<double(const void*)> m_fnFrequency;
    static void SortBufferData ( int nField, bool bAscending, IBMNetWorkGetService * pNetworkService, SBUFFERTYPE * bufferData, int nCntOfDataEntries );
    static int CompareBufferData ( const void* pEntry1, const void* pEntry2 );
};

template<typename SBUFFERTYPE>
EColumnType  CCANMsgWndDataHandler<SBUFFERTYPE>::m_nSortField = EColumnType::eNoColType;
template<typename SBUFFERTYPE>
int  CCANMsgWndDataHandler<SBUFFERTYPE>::m_nMFactor = -1;
template<typename SBUFFERTYPE>
IBMNetWorkGetService * CCANMsgWndDataHandler<SBUFFERTYPE>::m_pNetworkService = nullptr;
template<typename SBUFFERTYPE>
std::function<double(const void*)> CCANMsgWndDataHandler<SBUFFERTYPE>::m_fnFrequency;


template<typename SBUFFERTYPE>
void CCANMsgWndDataHandler<SBUFFERTYPE>::SortBufferData ( int nField, bool bAscending, IBMNetWorkGetService * pNetworkService, SBUFFERTYPE * bufferData, int nCntOfDataEntries )
{
    m_pNetworkService = pNetworkService;
    SetColumnField ( nField );
    m_nMFactor = bAscending ? 1 : -1;
    size_t tLenOfBuff = (size_t)nCntOfDataEntries;
    
    qsort ( (void*) bufferData, tLenOfBuff,
            sizeof ( SBUFFERTYPE ), CCANMsgWndDataHandler<SBUFFERTYPE>::CompareBufferData );
}

template<typename SBUFFERTYPE>
int CCANMsgWndDataHandler<SBUFFERTYPE>::CompareBufferData ( const void* pEntry1, const void* pEntry2 )
{
    int nRetValue = 0;

    STCANDATA* pCANDataEntry1 = (STCANDATA*) pEntry1;
    STCANDATA* pCANDataEntry2 = (STCANDATA*) pEntry2;
    const STCAN_MSG& sMsg1 = pCANDataEntry1->m_uDataInfo.m_sCANMsg;
    const STCAN_MSG& sMsg2 = pCANDataEntry2->m_uDataInfo.m_sCANMsg;

    // BUSMASTER-NG: values are compared, not subtracted (the difference of two 64 bit time stamps does not fit in an int)
    auto nCompare = [](INT64 n64First, INT64 n64Second) -> int
    {
        return (n64First < n64Second) ? -1 : ((n64First > n64Second) ? 1 : 0);
    };

    switch ( m_nSortField )
    {
        case EColumnType::eMsgDLCCol: // Sort by DLC
        {
            nRetValue = nCompare ( sMsg1.m_ucDataLen, sMsg2.m_ucDataLen );
            break;
        }
        case EColumnType::eMsgNameCol: // Sort by message name
        {
            // BUSMASTER-NG: the column shows the name from the database or, without one, the ID: messages with a name
            // come first, in alphabetical order, then those without, in order of ID
            CString msgName1, msgName2;
            bool bNamed1 = false, bNamed2 = false;
            IFrame* pouFrame = nullptr;
            if ( nullptr != m_pNetworkService )
            {
                ERRORCODE eResult = m_pNetworkService->GetFrame ( CAN, 0, sMsg1.m_unMsgID, nullptr, &pouFrame );
                if ( EC_SUCCESS == eResult && nullptr != pouFrame )
                {
                    std::string strName;
                    pouFrame->GetName ( strName );
                    msgName1 = strName.c_str();
                    bNamed1 = true;
                }

                pouFrame = nullptr;
                eResult = m_pNetworkService->GetFrame ( CAN, 0, sMsg2.m_unMsgID, nullptr, &pouFrame );
                if ( EC_SUCCESS == eResult && nullptr != pouFrame )
                {
                    std::string strName;
                    pouFrame->GetName ( strName );
                    msgName2 = strName.c_str();
                    bNamed2 = true;
                }
            }

            if ( bNamed1 && bNamed2 )
            {
                nRetValue = msgName1.CompareNoCase ( msgName2 );
            }
            else if ( bNamed1 != bNamed2 )
            {
                nRetValue = bNamed1 ? -1 : 1;
            }
            else
            {
                nRetValue = nCompare ( sMsg1.m_unMsgID, sMsg2.m_unMsgID );
            }
            break;
        }
        case EColumnType::eCANIDCol: // Sort by CAN id
        {
            nRetValue = nCompare ( sMsg1.m_unMsgID, sMsg2.m_unMsgID );
            break;
        }
        case EColumnType::eMsgTypeCol: // Sort by message type (standard, extended)
        {
            nRetValue = nCompare ( sMsg1.m_ucEXTENDED, sMsg2.m_ucEXTENDED );
            break;
        }
        case EColumnType::eChannelCol: // Sort by channel
        {
            nRetValue = nCompare ( sMsg1.m_ucChannel, sMsg2.m_ucChannel );
            break;
        }
        case EColumnType::eTxRxCol: // Sort by Tx/Rx column
        {
            nRetValue = nCompare ( IS_RX_MESSAGE ( pCANDataEntry1->m_ucDataType ) ? 1 : 0,
                                   IS_RX_MESSAGE ( pCANDataEntry2->m_ucDataType ) ? 1 : 0 );
            break;
        }
        case EColumnType::eFrequencyCol: // Sort by frequency (entries without a frequency come first when ascending)
        {
            if ( m_fnFrequency )
            {
                double dFreq1 = m_fnFrequency ( pEntry1 );
                double dFreq2 = m_fnFrequency ( pEntry2 );
                nRetValue = (dFreq1 < dFreq2) ? -1 : ((dFreq1 > dFreq2) ? 1 : 0);
            }
            break;
        }
        case EColumnType::eTimeStampCol: // Sort by time stamp
        {
            nRetValue = nCompare ( pCANDataEntry1->m_lTickCount.QuadPart, pCANDataEntry2->m_lTickCount.QuadPart );
        }
        break;
        default:
        {
        }
        break;
    }

    nRetValue *= m_nMFactor;

    // BUSMASTER-NG: entries that are equal in the column keep the same order each time they are sorted (qsort is not
    // stable), by ID, then channel, direction and type; the list is sorted again whenever an ID appears
    if ( 0 == nRetValue )
    {
        nRetValue = nCompare ( sMsg1.m_unMsgID, sMsg2.m_unMsgID );
    }
    if ( 0 == nRetValue )
    {
        nRetValue = nCompare ( sMsg1.m_ucChannel, sMsg2.m_ucChannel );
    }
    if ( 0 == nRetValue )
    {
        nRetValue = nCompare ( IS_RX_MESSAGE ( pCANDataEntry1->m_ucDataType ) ? 1 : 0,
                               IS_RX_MESSAGE ( pCANDataEntry2->m_ucDataType ) ? 1 : 0 );
    }
    if ( 0 == nRetValue )
    {
        nRetValue = nCompare ( sMsg1.m_ucEXTENDED, sMsg2.m_ucEXTENDED );
    }

    return nRetValue;
}


//class CLINMsgWndDataHandler
//{
//
//};
//
//class CJ1939MsgWndDataHandler
//{
//
//};