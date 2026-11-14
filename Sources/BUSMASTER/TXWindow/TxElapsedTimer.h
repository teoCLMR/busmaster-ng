/*
 * BUSMASTER-NG
 *
 * Timing of the repetition of the messages of the Transmit window.
 *
 * The transmit threads used to subtract 1 from the countdown of every message each time they woke up, taking a wait of
 * 1 ms for 1 ms. On Windows that wait lasts one or two ticks of the system timer (15.6 ms unless some program raised its
 * resolution), so a message every 10 ms went out every 20 ms or even slower, and how much slower changed from one PC to
 * another. Now each message has the moment, on the performance counter, at which it has to go out next
 * (TX_DETAILS::m_llNextDueTick), and the thread sleeps until the earliest of them on a high resolution timer where
 * Windows has one (CTxTicker), so the messages follow the clock whatever the wake-up rate is.
 */

#pragma once

#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// The performance counter, in ticks and in time
class CTxClock
{
public:
    static LONGLONG llNow()
    {
        LARGE_INTEGER liNow;
        QueryPerformanceCounter(&liNow);
        return liNow.QuadPart;
    }

    static LONGLONG llMsToTicks(int nMs)
    {
        LARGE_INTEGER liFrequency;
        QueryPerformanceFrequency(&liFrequency);
        return (LONGLONG) nMs * liFrequency.QuadPart / 1000;
    }

    // in units of 100 ns, as the waitable timers want it
    static LONGLONG ll100nsFromTicks(LONGLONG llTicks)
    {
        LARGE_INTEGER liFrequency;
        QueryPerformanceFrequency(&liFrequency);
        return llTicks * 10000000 / liFrequency.QuadPart;
    }
};

// Sleep of a transmit thread until the next message is due. Windows 10 1803 and later have a waitable timer with a
// resolution of well under 1 ms; with it the thread wakes up when a message is due. Without it (Windows 7, early
// Windows 10) the thread waits 1 ms with the system timer at its finest, which lasts 1 to 2 ms, so the shortest
// repetition time that can be kept is 2 ms.
class CTxTicker
{
private:
    HANDLE m_hTimer;

    static bool bHasHighResolutionTimer()
    {
        // not GetVersionEx, which tells Windows 8 to a program that is not declared for newer versions
        typedef LONG (WINAPI *PFN_RTLGETVERSION)(OSVERSIONINFOW*);
        HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
        PFN_RTLGETVERSION pfnRtlGetVersion =
            (nullptr != hNtDll) ? (PFN_RTLGETVERSION) GetProcAddress(hNtDll, "RtlGetVersion") : nullptr;
        if (nullptr == pfnRtlGetVersion)
        {
            return false;
        }
        OSVERSIONINFOW sInfo = { sizeof(sInfo) };
        if (0 != pfnRtlGetVersion(&sInfo))
        {
            return false;
        }
        // Windows 10 1803 is build 17134
        return (sInfo.dwMajorVersion > 10) || ((10 == sInfo.dwMajorVersion) && (sInfo.dwBuildNumber >= 17134));
    }

public:
    CTxTicker() : m_hTimer(nullptr)
    {
    }

    ~CTxTicker()
    {
        if (nullptr != m_hTimer)
        {
            CloseHandle(m_hTimer);
        }
    }

    // Shortest repetition time, in ms, that the transmit threads can keep
    static int nMinimumPeriodMs()
    {
        static const int nMinimum = bHasHighResolutionTimer() ? 1 : 2;
        return nMinimum;
    }

    // Creates the high resolution timer; false when this Windows has none (the caller waits 1 ms as before)
    bool bStart()
    {
        if (!bHasHighResolutionTimer())
        {
            return false;
        }
        m_hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        return nullptr != m_hTimer;
    }

    // The timer goes off once, after ll100ns (units of 100 ns)
    void ArmOnce(LONGLONG ll100ns)
    {
        if (nullptr != m_hTimer)
        {
            LARGE_INTEGER liDue;
            liDue.QuadPart = -((ll100ns < 1) ? 1 : ll100ns);   // negative: relative to now
            SetWaitableTimer(m_hTimer, &liDue, 0, nullptr, nullptr, FALSE);
        }
    }

    void Pause()
    {
        if (nullptr != m_hTimer)
        {
            CancelWaitableTimer(m_hTimer);
        }
    }

    HANDLE hGetHandle() const
    {
        return m_hTimer;
    }
};
