/**
 * \file      VCRuntimeCheck.cpp
 * \brief     Entry point that checks the Visual C++ 2013 runtime before startup.
 *
 * libxml2.dll and DBManager.dll are prebuilt with Visual C++ 2013 and need
 * msvcr120.dll and msvcp120.dll. Without that runtime Windows stops the process
 * with a bare "msvcr120.dll is missing" error, before any code of ours runs.
 * The DLLs that pull it in are delay-loaded (see BUSMASTER.vcxproj), so this
 * entry point runs first, explains what to install and then starts the normal
 * CRT/MFC startup.
 */

#include <windows.h>
#include <shellapi.h>

extern "C" int WinMainCRTStartup();

namespace
{
const wchar_t VCREDIST_2013_URL[] = L"https://aka.ms/highdpimfc2013x86enu";

bool bIsRuntimeInstalled()
{
    const wchar_t* acDlls[] = { L"msvcr120.dll", L"msvcp120.dll" };
    for (const wchar_t* pcDll : acDlls)
    {
        HMODULE hModule = LoadLibraryW(pcDll);
        if (hModule == nullptr)
        {
            return false;
        }
        FreeLibrary(hModule);
    }
    return true;
}

void vReportMissingRuntime()
{
    bool bItalian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ITALIAN;
    const wchar_t* pcText = bItalian
        ? L"BUSMASTER-NG richiede il pacchetto Microsoft Visual C++ 2013 "
          L"Redistributable (x86), che non risulta installato.\n\n"
          L"Installarlo e poi riavviare BUSMASTER-NG. L'installer di "
          L"BUSMASTER-NG lo installa automaticamente.\n\n"
          L"Aprire ora la pagina di download Microsoft?"
        : L"BUSMASTER-NG needs the Microsoft Visual C++ 2013 Redistributable "
          L"(x86), which is not installed.\n\n"
          L"Install it and then start BUSMASTER-NG again. The BUSMASTER-NG "
          L"installer installs it automatically.\n\n"
          L"Open the Microsoft download page now?";
    const wchar_t* pcTitle = bItalian ? L"BUSMASTER-NG - Componente mancante"
                                      : L"BUSMASTER-NG - Missing component";

    if (MessageBoxW(nullptr, pcText, pcTitle, MB_YESNO | MB_ICONERROR) == IDYES)
    {
        ShellExecuteW(nullptr, L"open", VCREDIST_2013_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }
}
}

// libxml2 exports xmlFree as a variable (a function pointer), and the linker
// cannot delay-load data. This file defines the import slot __imp__xmlFree
// itself, so the linker never takes it from libxml2.lib: the slot points to a
// proxy that reads the real variable from the delay-loaded libxml2.dll.
namespace
{
typedef void (__cdecl* XmlFreeFunc)(void*);

void __cdecl vXmlFreeProxy(void* pvMem)
{
    static XmlFreeFunc* ppfnXmlFree = nullptr;
    if (ppfnXmlFree == nullptr)
    {
        HMODULE hLibXml = GetModuleHandleW(L"libxml2.dll");
        if (hLibXml == nullptr)
        {
            hLibXml = LoadLibraryW(L"libxml2.dll");
        }
        ppfnXmlFree = reinterpret_cast<XmlFreeFunc*>(GetProcAddress(hLibXml, "xmlFree"));
    }
    (*ppfnXmlFree)(pvMem);
}

XmlFreeFunc g_pfnXmlFree = vXmlFreeProxy;
}

extern "C" XmlFreeFunc* _imp__xmlFree = &g_pfnXmlFree;

// Runs before the CRT is initialised: only plain Win32 calls are allowed here.
extern "C" __declspec(safebuffers) int BusmasterEntry()
{
    if (!bIsRuntimeInstalled())
    {
        vReportMissingRuntime();
        ExitProcess(1);
    }
    return WinMainCRTStartup();
}
