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
* \file      LinTransmitter.cpp
* \author    Robin G.K.
* \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
*/


#pragma once
#include "LINTransmitter.h"
#include "BaseDIL_LIN.h"
#include <mmsystem.h>   // timeBeginPeriod, linked with Winmm.lib
//#include "..\DIL_Interface\DIL_Interface_extern.h"

CLINTransmitter* CLINTransmitter::m_pouLINTransmitter = nullptr;
int CLINTransmitter::m_nRefCounter = 0;

DWORD WINAPI CLINTransmitter::TransmitThread(LPVOID pVoid)
{
    CPARAM_THREADPROC* pThreadParam = (CPARAM_THREADPROC*)pVoid;
    if (pThreadParam == nullptr)
    {
        return (DWORD)-1;
    }
    CLINTransmitter* pouData = (CLINTransmitter*)pThreadParam->m_pBuffer;

    if (pouData == nullptr)
    {
        return (DWORD)-1;
    }

    bool bLoopON = true;

    // BUSMASTER-NG: sleep until the next message is due on a high resolution timer where Windows has one (Windows 10
    // 1803 or later); otherwise wait 1 ms with the system timer at its finest, which lasts 1 to 2 ms (see
    // TxElapsedTimer.h)
    CTxTicker ouTicker;
    bool bHighResolution = ouTicker.bStart();
    HANDLE ahWakeUp[2] = { pThreadParam->m_hActionEvent, ouTicker.hGetHandle() };
    if (!bHighResolution)
    {
        timeBeginPeriod(1);
    }

    while (bLoopON)
    {
        if (bHighResolution)
        {
            // sleep until the first message is due, but not for more than 5 ms: a message that is enabled or changed in
            // the meantime must not wait for the end of it
            LONGLONG llWaitTicks = CTxClock::llMsToTicks(5);
            if (pouData->m_llNextWakeUp > 0)
            {
                LONGLONG llUntilDue = pouData->m_llNextWakeUp - CTxClock::llNow();
                if (llUntilDue < llWaitTicks)
                {
                    llWaitTicks = llUntilDue;
                }
            }
            if (llWaitTicks > 0)
            {
                ouTicker.ArmOnce(CTxClock::ll100nsFromTicks(llWaitTicks));
                WaitForMultipleObjects(2, ahWakeUp, FALSE, INFINITE);
            }
            else
            {
                // already due: only look whether there is something else to do
                WaitForSingleObject(pThreadParam->m_hActionEvent, 0);
            }
        }
        else
        {
            WaitForSingleObject(pThreadParam->m_hActionEvent, 1);
        }
        switch (pThreadParam->m_unActionCode)
        {
            case INVOKE_FUNCTION:
            {
                pouData->TransmitAll();
            }
            break;

            case SUSPEND:
            {
                // nothing to send while disconnected: no need to keep the timer fast
                if (bHighResolution)
                {
                    ouTicker.Pause();
                }
                else
                {
                    timeEndPeriod(1);
                }
                WaitForSingleObject(pThreadParam->m_hActionEvent, INFINITE);
                if (!bHighResolution)
                {
                    timeBeginPeriod(1);
                }
            }
            break;

            case EXIT_THREAD:
            {
                bLoopON = false;
            }
            break;

            default:
                break;
        }
    }
    if (!bHighResolution)
    {
        timeEndPeriod(1);
    }
    SetEvent(pThreadParam->hGetExitNotifyEvent());
    return 0;
}

CLINTransmitter::CLINTransmitter()
{
    m_ulClientId = 0;
    m_pouTxDataStore = nullptr;
    m_eBusStatus = BUS_DISCONNECTED;
    m_ouTransmitThread.m_pBuffer = this;
    m_ouTransmitThread.m_hActionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_llNextWakeUp = 0;
    m_bRestartSchedule = true;

}
CLINTransmitter::~CLINTransmitter()
{
    m_ouTransmitThread.bTerminateThread();
}
int CLINTransmitter::nGetMsgAt(int nIndex, STLIN_MSG& ouLinMsg)
{
    int hResult = S_FALSE;
    ITxMsgItem* pouMsgItem = nullptr;
    if (nullptr == m_pouTxDataStore)
    {
        return hResult;
    }
    m_pouTxDataStore->GetMsgItem(nIndex, pouMsgItem);
    if (nullptr == pouMsgItem)
    {
        return hResult;
    }
    hResult = S_OK;
    ouLinMsg.m_ucChannel = pouMsgItem->MsgDetails.nChannel;
    memcpy(ouLinMsg.m_ucData, pouMsgItem->MsgDetails.pchData, pouMsgItem->MsgDetails.nDLC);
    ouLinMsg.m_ucDataLen = pouMsgItem->MsgDetails.nDLC;
    ouLinMsg.m_ucMsgID = pouMsgItem->MsgDetails.nMsgId;
    ouLinMsg.m_ucMsgTyp = pouMsgItem->MsgDetails.nMsgType;

    return hResult;
}
ITransmitter* CLINTransmitter::GetInstance()
{
    if (nullptr == m_pouLINTransmitter)
    {
        m_pouLINTransmitter = new CLINTransmitter();
    }
    m_nRefCounter++;
    return m_pouLINTransmitter;
}
void CLINTransmitter::ReleaseInstance()
{
    m_nRefCounter--;
    if (nullptr != m_pouLINTransmitter && 0 == m_nRefCounter)
    {
        delete m_pouLINTransmitter;
        m_pouLINTransmitter = nullptr;
    }
}
int CLINTransmitter::nPreMessageTransmission()
{
    int hResult = S_FALSE;
    if (nullptr == m_pouTxDataStore)
    {
        return hResult;
    }
    int nSize = m_pouTxDataStore->GetMsgItemCount();
    for (int nCount = 0; nCount < nSize; nCount++)
    {
        TransmitAt(nCount);
    }
    return hResult;
}
int CLINTransmitter::SetTxDataStore(CTxDataStore* pouTxDataStore)
{
    int hResult = S_OK;
    m_pouTxDataStore = pouTxDataStore;
    return hResult;
}
int CLINTransmitter::SetClientId(unsigned long ulClientId)
{
    int hResult = S_OK;
    m_ulClientId = ulClientId;
    return hResult;
}
int CLINTransmitter::OnBusStatusChanged(ESTATUS_BUS eBusStatus)
{
    int hResult = S_FALSE;
    m_eBusStatus = eBusStatus;

    if (BUS_PRECONNECT == eBusStatus)
    {
        hResult = S_OK;
        nPreMessageTransmission();
    }

    else if (BUS_CONNECTED == eBusStatus)
    {
        hResult = S_OK;
        // BUSMASTER-NG: the schedule of the messages starts from the moment of the connection
        m_bRestartSchedule = true;
        m_llNextWakeUp = 0;
        m_ouTransmitThread.m_unActionCode = INVOKE_FUNCTION;
        m_ouTransmitThread.bStartThread(TransmitThread);
        SetEvent(m_ouTransmitThread.m_hActionEvent);
    }
    else if (BUS_DISCONNECTED == eBusStatus)
    {
        hResult = S_OK;
        m_ouTransmitThread.m_unActionCode = SUSPEND;
    }
    return hResult;
}
int CLINTransmitter::TransmitAt(int nIndex)
{

    HRESULT hResult = S_FALSE;
    CBaseDIL_LIN* pouBaseDIL_LIN = nullptr;
    DIL_GetInterface(LIN, (void**)&pouBaseDIL_LIN);

    if (nullptr == pouBaseDIL_LIN)
    {
        return hResult;
    }

    STLIN_MSG ouMsg;
    if (S_OK == nGetMsgAt(nIndex,ouMsg))
    {
        hResult = pouBaseDIL_LIN->DILL_SendMsg(m_ulClientId, ouMsg);
    }

    return hResult;
}
int CLINTransmitter::TransmitAll()
{
    int hResult = S_FALSE;
    if (nullptr == m_pouTxDataStore)
    {
        return hResult;
    }
    hResult = S_OK;

    // BUSMASTER-NG: every message has the moment (performance counter) at which it goes out next, and it goes out when
    // that moment has come, so the time between two messages follows the clock and does not depend on how often this
    // function is called. The moment of the next one is the one just gone by plus the period, so the period does not
    // stretch; a message that is late goes out again for the periods it missed, up to three in a row, and one that is
    // later than that starts again from now. No period below what the timer can keep (see TxElapsedTimer.h).
    bool bRestart = m_bRestartSchedule;
    m_bRestartSchedule = false;
    LONGLONG llNow = CTxClock::llNow();
    LONGLONG llNextWakeUp = 0;

    int nSize = m_pouTxDataStore->GetMsgItemCount();
    ITxMsgItem* pouMsgItem = nullptr;
    for (int nCount = 0; nCount < nSize; nCount++)
    {
        m_pouTxDataStore->GetMsgItem(nCount, pouMsgItem);
        if (nullptr != pouMsgItem)
        {
            TX_DETAILS& ouTx = pouMsgItem->TxDetails;
            if (true == ouTx.m_bTimerEnabled)
            {
                int nPeriodMs = ouTx.nActualTimer;
                if (nPeriodMs < CTxTicker::nMinimumPeriodMs())
                {
                    nPeriodMs = CTxTicker::nMinimumPeriodMs();
                }
                LONGLONG llPeriod = CTxClock::llMsToTicks(nPeriodMs);
                if (bRestart || (0 == ouTx.m_llNextDueTick) || (nPeriodMs != ouTx.m_nDuePeriodMs))
                {
                    // new connection, repetition just enabled or its time changed: counts from now
                    ouTx.m_llNextDueTick = llNow + llPeriod;
                    ouTx.m_nDuePeriodMs = nPeriodMs;
                }
                int nSent = 0;
                while ((llNow >= ouTx.m_llNextDueTick) && (nSent < 3))
                {
                    TransmitAt(nCount);
                    ouTx.m_llNextDueTick += llPeriod;
                    nSent++;
                }
                if (llNow >= ouTx.m_llNextDueTick)
                {
                    ouTx.m_llNextDueTick = CTxClock::llNow() + llPeriod;
                }
                m_pouTxDataStore->SetMsgItem(nCount, pouMsgItem);
                if ((0 == llNextWakeUp) || (ouTx.m_llNextDueTick < llNextWakeUp))
                {
                    llNextWakeUp = ouTx.m_llNextDueTick;
                }
            }
            else
            {
                // counts from now when the repetition is enabled again
                ouTx.m_llNextDueTick = 0;
            }
        }
    }
    m_llNextWakeUp = llNextWakeUp;
    return hResult;
}
int CLINTransmitter::OnKeyPressed(char chKey)
{
    int hResult = S_FALSE;
    if (nullptr == m_pouTxDataStore)
    {
        return hResult;
    }

    int nSize = m_pouTxDataStore->GetMsgItemCount();
    ITxMsgItem* pouMsgItem = nullptr;
    if (BUS_CONNECTED == m_eBusStatus)
    {
        for (int nCount = 0; nCount < nSize; nCount++)
        {
            pouMsgItem = nullptr;
            m_pouTxDataStore->GetMsgItem(nCount, pouMsgItem);
            if (nullptr != pouMsgItem)
            {
                if (true == pouMsgItem->TxDetails.bKeyEnabled && pouMsgItem->TxDetails.m_chKeyVal == chKey)
                {
                    hResult = S_OK;
                    TransmitAt(nCount);
                }
            }
        }
    }

    return hResult;
}