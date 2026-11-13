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
 * \file      UpdateCheck.cpp
 * \brief     BUSMASTER-NG: Help > Check for Updates and Check at Startup
 *
 * WinINet rather than WinHTTP: it uses the proxy settings of Windows (including automatic configuration scripts)
 * and TLS 1.2 also on Windows 7, which GitHub requires.
 *
 * The downloaded installer is checked against the SHA-256 digest that GitHub computes for every release asset. That
 * catches a damaged or incomplete download; it is not a proof of origin, since the digest comes from the same
 * release. Once the releases are signed (SignPath), the Authenticode signature of the installer is to be checked too.
 */
#include "stdafx.h"
#include <wininet.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <vector>
#include "../../BusmasterNGVersion.h"
#include "UpdateCheck.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")

// latest published release: drafts and pre-releases are never returned
#define UPDATE_API_URL          "https://api.github.com/repos/teoCLMR/busmaster-ng/releases/latest"
#define UPDATE_RELEASES_URL     "https://github.com/teoCLMR/busmaster-ng/releases"
#define UPDATE_DOWNLOAD_PREFIX  "https://github.com/teoCLMR/busmaster-ng/releases/download/"
#define UPDATE_TIMEOUT_MS       10000
#define UPDATE_MAX_REPLY        (1024 * 1024)

// where the installer (Tools\installer.iss, AppId) records the folder it installed to
#define UPDATE_UNINSTALL_KEY    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{EBEBA200-89AC-4FB2-BC8F-B931C7753CE5}_is1"
#define UPDATE_SETTINGS_KEY     "Software\\BUSMASTER-NG\\Updates"
#define UPDATE_STARTUP_VALUE    "CheckAtStartup"

// installer without wizard pages (progress only); /RELAUNCH=1 makes it start BUSMASTER-NG again (installer.iss)
#define UPDATE_INSTALLER_ARGS   "/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RELAUNCH=1"

const UINT WM_BMNG_UPDATE_FOUND = ::RegisterWindowMessage("BUSMASTER-NG.UpdateFound");

struct SRelease
{
    CStringA m_omTag;           // "v3.5.0"
    CStringA m_omVersion;       // "3.5.0"
    int      m_anVersion[4];
    CStringA m_omInstallerName; // "BUSMASTER-NG_3.5.0_setup.exe", empty when the release has no installer
    CStringA m_omInstallerUrl;
    CStringA m_omSha256;        // lower-case hex, empty when GitHub gave no digest
    ULONGLONG m_ullSize = 0;
};

/* Opens a WinINet session with the timeouts of the update check. */
static HINTERNET hOpenSession(CString& omError)
{
    HINTERNET hInternet = ::InternetOpen(BMNG_PRODUCT_NAME "/" BMNG_VERSION_STRING,
                                         INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (hInternet == nullptr)
    {
        omError.Format("the Internet functions of Windows are not available, error %lu", ::GetLastError());
        return nullptr;
    }
    DWORD dwTimeout = UPDATE_TIMEOUT_MS;
    ::InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    ::InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    ::InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    return hInternet;
}

/* Opens pcUrl and checks the HTTP status. On failure omError says why, in words for the user. */
static HINTERNET hOpenUrl(HINTERNET hInternet, const char* pcUrl, const char* pcHeaders, CString& omError)
{
    HINTERNET hRequest = ::InternetOpenUrl(hInternet, pcUrl, pcHeaders, pcHeaders ? (DWORD)strlen(pcHeaders) : 0,
                                           INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                           INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI, 0);
    if (hRequest == nullptr)
    {
        DWORD dwError = ::GetLastError();
        if (dwError == ERROR_INTERNET_TIMEOUT)
        {
            omError = "GitHub did not answer in time";
        }
        else
        {
            omError.Format("no connection to GitHub, error %lu", dwError);
        }
        return nullptr;
    }
    DWORD dwStatus = 0;
    DWORD dwSize = sizeof(dwStatus);
    ::HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatus, &dwSize, nullptr);
    if (dwStatus != HTTP_STATUS_OK)
    {
        if (dwStatus == HTTP_STATUS_NOT_FOUND)
        {
            omError = "it is not on GitHub (HTTP status 404)";
        }
        else
        {
            omError.Format("GitHub answered with HTTP status %lu", dwStatus);
        }
        ::InternetCloseHandle(hRequest);
        return nullptr;
    }
    return hRequest;
}

/* Reads the release information. On failure omError says why, in words for the user. */
static bool bDownloadLatestRelease(CStringA& omReply, CString& omError)
{
    HINTERNET hInternet = hOpenSession(omError);
    if (hInternet == nullptr)
    {
        return false;
    }
    bool bResult = false;
    HINTERNET hRequest = hOpenUrl(hInternet, UPDATE_API_URL, "Accept: application/vnd.github+json\r\n", omError);
    if (hRequest != nullptr)
    {
        std::vector<char> acBuffer(16 * 1024);
        DWORD dwRead = 0;
        while (::InternetReadFile(hRequest, acBuffer.data(), (DWORD)acBuffer.size(), &dwRead) && dwRead > 0)
        {
            omReply.Append(acBuffer.data(), (int)dwRead);
            if (omReply.GetLength() > UPDATE_MAX_REPLY)
            {
                break;
            }
        }
        bResult = !omReply.IsEmpty();
        if (!bResult)
        {
            omError = "the answer of GitHub could not be read";
        }
        ::InternetCloseHandle(hRequest);
    }
    ::InternetCloseHandle(hInternet);
    return bResult;
}

/* Value of the string field pcField of a JSON object (GitHub never escapes characters in tags, names, addresses
   and digests). */
static CStringA omGetJsonString(const CStringA& omJson, const char* pcField)
{
    CStringA omKey;
    omKey.Format("\"%s\"", pcField);
    int nPos = omJson.Find(omKey);
    if (nPos < 0)
    {
        return "";
    }
    nPos += omKey.GetLength();
    while (nPos < omJson.GetLength() && (isspace((unsigned char)omJson[nPos]) || omJson[nPos] == ':'))
    {
        nPos++;
    }
    if (nPos >= omJson.GetLength() || omJson[nPos] != '"')
    {
        return "";
    }
    int nEnd = omJson.Find('"', nPos + 1);
    return nEnd < 0 ? CStringA() : omJson.Mid(nPos + 1, nEnd - nPos - 1);
}

/* Number value of the field pcField of a JSON object, 0 when missing. */
static ULONGLONG ullGetJsonNumber(const CStringA& omJson, const char* pcField)
{
    CStringA omKey;
    omKey.Format("\"%s\"", pcField);
    int nPos = omJson.Find(omKey);
    if (nPos < 0)
    {
        return 0;
    }
    nPos += omKey.GetLength();
    while (nPos < omJson.GetLength() && (isspace((unsigned char)omJson[nPos]) || omJson[nPos] == ':'))
    {
        nPos++;
    }
    return _strtoui64((const char*)omJson + nPos, nullptr, 10);
}

/* The JSON object (text from '{' to '}') that has the field "name" with the value omName: one asset of the
   release. Strings are skipped while scanning, so the same text in the release notes does not match. */
static CStringA omFindObjectByName(const CStringA& omJson, const CStringA& omName)
{
    std::vector<int> anObjectStarts;
    int nMatchDepth = -1;
    CStringA omLastKey;
    int nLength = omJson.GetLength();
    for (int i = 0; i < nLength; i++)
    {
        char c = omJson[i];
        if (c == '{')
        {
            anObjectStarts.push_back(i);
        }
        else if (c == '}' && !anObjectStarts.empty())
        {
            if ((int)anObjectStarts.size() == nMatchDepth)
            {
                return omJson.Mid(anObjectStarts.back(), i - anObjectStarts.back() + 1);
            }
            anObjectStarts.pop_back();
        }
        else if (c == '"')
        {
            int nStart = i + 1;
            for (i = nStart; i < nLength && omJson[i] != '"'; i++)
            {
                if (omJson[i] == '\\')
                {
                    i++;
                }
            }
            CStringA omString = omJson.Mid(nStart, i - nStart);
            int nNext = i + 1;
            while (nNext < nLength && isspace((unsigned char)omJson[nNext]))
            {
                nNext++;
            }
            if (nNext < nLength && omJson[nNext] == ':')
            {
                omLastKey = omString;
            }
            else if (omLastKey == "name" && omString == omName && nMatchDepth < 0)
            {
                nMatchDepth = (int)anObjectStarts.size();
            }
        }
    }
    return "";
}

/* "v3.4.2" or "3.4.2" -> { 3, 4, 2, 0 }. False when the text is not such a version. */
static bool bParseVersion(const CStringA& omText, int anVersion[4])
{
    const char* pcText = omText;
    if (*pcText == 'v' || *pcText == 'V')
    {
        pcText++;
    }
    for (int i = 0; i < 4; i++)
    {
        anVersion[i] = 0;
    }
    for (int i = 0; i < 4; i++)
    {
        if (!isdigit((unsigned char)*pcText))
        {
            return false;
        }
        char* pcEnd = nullptr;
        anVersion[i] = (int)strtol(pcText, &pcEnd, 10);
        pcText = pcEnd;
        if (*pcText != '.')
        {
            return *pcText == '\0';
        }
        pcText++;
    }
    return false;
}

static int nCompareVersions(const int anFirst[4], const int anSecond[4])
{
    for (int i = 0; i < 4; i++)
    {
        if (anFirst[i] != anSecond[i])
        {
            return anFirst[i] < anSecond[i] ? -1 : 1;
        }
    }
    return 0;
}

/* Latest release, with its installer when it has one. On failure omError says why, in words for the user. */
static bool bGetLatestRelease(SRelease& sRelease, CString& omError)
{
    CStringA omReply;
    if (!bDownloadLatestRelease(omReply, omError))
    {
        return false;
    }
    sRelease.m_omTag = omGetJsonString(omReply, "tag_name");
    if (!bParseVersion(sRelease.m_omTag, sRelease.m_anVersion))
    {
        omError = "GitHub did not give the version of the latest release";
        return false;
    }
    sRelease.m_omVersion = sRelease.m_omTag;
    sRelease.m_omVersion.TrimLeft("vV");

    // same name as OutputBaseFilename of Tools\installer.iss
    CStringA omName = BMNG_PRODUCT_NAME "_" + sRelease.m_omVersion + "_setup.exe";
    CStringA omAsset = omFindObjectByName(omReply, omName);
    CStringA omUrl = omGetJsonString(omAsset, "browser_download_url");
    if (!omAsset.IsEmpty() && omUrl == UPDATE_DOWNLOAD_PREFIX + sRelease.m_omTag + "/" + omName)
    {
        sRelease.m_omInstallerName = omName;
        sRelease.m_omInstallerUrl = omUrl;
        sRelease.m_ullSize = ullGetJsonNumber(omAsset, "size");
        CStringA omDigest = omGetJsonString(omAsset, "digest");
        if (omDigest.Left(7).CompareNoCase("sha256:") == 0 && omDigest.GetLength() == 7 + 64)
        {
            sRelease.m_omSha256 = omDigest.Mid(7);
            sRelease.m_omSha256.MakeLower();
        }
    }
    return true;
}

static bool bIsNewerThanRunning(const SRelease& sRelease)
{
    int anRunning[4];
    bParseVersion(BMNG_VERSION_STRING, anRunning);
    return nCompareVersions(sRelease.m_anVersion, anRunning) > 0;
}

/* True when this BUSMASTER.exe is the one the BUSMASTER-NG installer put on this PC (for all users or only for
   this user), false for a portable copy: running the installer would add a second copy instead of updating it. */
static bool bIsInstalledCopy()
{
    char acExe[MAX_PATH] = { 0 };
    ::GetModuleFileName(nullptr, acExe, MAX_PATH);
    CString omExeFolder = acExe;
    omExeFolder = omExeFolder.Left(omExeFolder.ReverseFind('\\'));

    HKEY ahRoots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY hRoot : ahRoots)
    {
        char acFolder[MAX_PATH] = { 0 };
        DWORD dwSize = sizeof(acFolder) - 1;
        if (::RegGetValue(hRoot, UPDATE_UNINSTALL_KEY, "InstallLocation", RRF_RT_REG_SZ, nullptr, acFolder, &dwSize)
                == ERROR_SUCCESS)
        {
            CString omFolder = acFolder;
            omFolder.TrimRight('\\');
            if (!omFolder.IsEmpty() && omFolder.CompareNoCase(omExeFolder) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static void vOpenUrl(const CString& omUrl)
{
    if (::ShellExecute(nullptr, "open", omUrl, nullptr, nullptr, SW_SHOWNORMAL) <= (HINSTANCE)32)
    {
        AfxMessageBox("The browser could not be started. The releases of BUSMASTER-NG are at\n" UPDATE_RELEASES_URL,
                      MB_OK | MB_ICONWARNING);
    }
}

static CString omReleasePage(const SRelease& sRelease)
{
    // the tag has passed bParseVersion: only a 'v', digits and dots go into the address
    return CString(UPDATE_RELEASES_URL "/tag/") + CString(sRelease.m_omTag);
}

static CStringA omToHex(const BYTE* pbData, DWORD dwLength)
{
    CStringA omHex;
    for (DWORD i = 0; i < dwLength; i++)
    {
        omHex.AppendFormat("%02x", pbData[i]);
    }
    return omHex;
}

/* State shared by bDownloadInstaller (user interface thread) and dwDownloadThread. */
struct SDownload
{
    CStringA m_omUrl;
    CString m_omPath;
    volatile LONGLONG m_llDone = 0;     // bytes received so far
    volatile LONG m_lCancel = 0;        // set by the user interface thread
    CStringA m_omSha256;                // result: lower-case hex of what was received
    CString m_omError;                  // result: empty when the download is complete (or was cancelled)
};

/* Downloads m_omUrl to m_omPath and computes its SHA-256, in a worker thread: connecting to GitHub alone can take
   seconds, and the user interface thread has to keep the progress dialog going meanwhile. */
static DWORD WINAPI dwDownloadThread(LPVOID pvDownload)
{
    SDownload& sDownload = *(SDownload*)pvDownload;
    BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ||
            !BCRYPT_SUCCESS(::BCryptCreateHash(hAlgorithm, &hHash, nullptr, 0, nullptr, 0, 0)))
    {
        sDownload.m_omError = "Windows could not compute the SHA-256 of the installer";
        if (hAlgorithm != nullptr)
        {
            ::BCryptCloseAlgorithmProvider(hAlgorithm, 0);
        }
        return 0;
    }

    HINTERNET hInternet = hOpenSession(sDownload.m_omError);
    HINTERNET hRequest = nullptr;
    if (hInternet != nullptr)
    {
        hRequest = hOpenUrl(hInternet, sDownload.m_omUrl, nullptr, sDownload.m_omError);
        if (hRequest == nullptr)
        {
            sDownload.m_omError = "the installer could not be downloaded: " + sDownload.m_omError;
        }
    }
    HANDLE hFile = INVALID_HANDLE_VALUE;
    if (hRequest != nullptr)
    {
        hFile = ::CreateFile(sDownload.m_omPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            sDownload.m_omError.Format("the installer could not be saved as %s, error %lu",
                                       (LPCTSTR)sDownload.m_omPath, ::GetLastError());
        }
    }

    std::vector<BYTE> abBuffer(64 * 1024);
    while (hFile != INVALID_HANDLE_VALUE && sDownload.m_lCancel == 0)
    {
        DWORD dwRead = 0;
        if (!::InternetReadFile(hRequest, abBuffer.data(), (DWORD)abBuffer.size(), &dwRead))
        {
            sDownload.m_omError.Format("the download stopped, error %lu", ::GetLastError());
            break;
        }
        if (dwRead == 0)
        {
            BYTE abDigest[32] = { 0 };
            ::BCryptFinishHash(hHash, abDigest, sizeof(abDigest), 0);
            sDownload.m_omSha256 = omToHex(abDigest, sizeof(abDigest));
            break;
        }
        DWORD dwWritten = 0;
        if (!::WriteFile(hFile, abBuffer.data(), dwRead, &dwWritten, nullptr) || dwWritten != dwRead)
        {
            sDownload.m_omError.Format("the installer could not be saved as %s, error %lu",
                                       (LPCTSTR)sDownload.m_omPath, ::GetLastError());
            break;
        }
        ::BCryptHashData(hHash, abBuffer.data(), dwRead, 0);
        ::InterlockedExchangeAdd64(&sDownload.m_llDone, dwRead);
    }

    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
    }
    if (hRequest != nullptr)
    {
        ::InternetCloseHandle(hRequest);
    }
    if (hInternet != nullptr)
    {
        ::InternetCloseHandle(hInternet);
    }
    ::BCryptDestroyHash(hHash);
    ::BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    return 0;
}

/* Downloads the installer to %TEMP%\BUSMASTER-NG update, showing the progress, and checks its size and SHA-256.
   False with an empty omError when the user cancelled. */
static bool bDownloadInstaller(HWND hParent, const SRelease& sRelease, CString& omPath, CString& omError)
{
    char acTemp[MAX_PATH] = { 0 };
    ::GetTempPath(MAX_PATH, acTemp);
    CString omFolder = CString(acTemp) + BMNG_PRODUCT_NAME " update";
    ::CreateDirectory(omFolder, nullptr);
    omPath = omFolder + "\\" + CString(sRelease.m_omInstallerName);

    SDownload sDownload;
    sDownload.m_omUrl = sRelease.m_omInstallerUrl;
    sDownload.m_omPath = omPath;

    // Windows' own progress dialog, which runs in a thread of its own
    IProgressDialog* pProgress = nullptr;
    ::CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pProgress));
    if (pProgress != nullptr)
    {
        CStringW omLine;
        omLine.Format(L"Downloading %S %S", BMNG_PRODUCT_NAME, (LPCSTR)sRelease.m_omVersion);
        pProgress->SetTitle(L"" BMNG_PRODUCT_NAME " update");
        pProgress->SetLine(1, omLine, FALSE, nullptr);
        pProgress->SetLine(2, L"Connecting to GitHub...", FALSE, nullptr);
        pProgress->SetCancelMsg(L"Cancelling...", nullptr);
        pProgress->StartProgressDialog(hParent, nullptr, PROGDLG_MODAL | PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
    }

    HANDLE hThread = ::CreateThread(nullptr, 0, dwDownloadThread, &sDownload, 0, nullptr);
    if (hThread == nullptr)
    {
        sDownload.m_omError.Format("the download could not be started, error %lu", ::GetLastError());
    }
    else
    {
        bool bConnected = false;
        for (;;)
        {
            // only messages sent by other threads (the progress dialog disables hParent from its own thread):
            // the rest of BUSMASTER-NG waits, as behind any modal dialog
            DWORD dwWait = ::MsgWaitForMultipleObjects(1, &hThread, FALSE, 100, QS_SENDMESSAGE);
            MSG sMsg;
            ::PeekMessage(&sMsg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
            ULONGLONG ullDone = (ULONGLONG)sDownload.m_llDone;
            if (pProgress != nullptr)
            {
                if (!bConnected && ullDone > 0)
                {
                    bConnected = true;
                    CStringW omSize;
                    omSize.Format(L"%.1f MB", sRelease.m_ullSize / (1024.0 * 1024.0));
                    pProgress->SetLine(2, omSize, FALSE, nullptr);
                }
                pProgress->SetProgress64(ullDone, sRelease.m_ullSize);
                if (pProgress->HasUserCancelled())
                {
                    ::InterlockedExchange(&sDownload.m_lCancel, 1);
                }
            }
            if (dwWait == WAIT_OBJECT_0)
            {
                break;
            }
        }
        ::CloseHandle(hThread);
    }

    if (pProgress != nullptr)
    {
        pProgress->StopProgressDialog();
        pProgress->Release();
    }

    bool bResult = false;
    if (sDownload.m_lCancel != 0)
    {
        omError.Empty();
    }
    else if (!sDownload.m_omError.IsEmpty())
    {
        omError = sDownload.m_omError;
    }
    else if ((ULONGLONG)sDownload.m_llDone != sRelease.m_ullSize)
    {
        omError.Format("the download is incomplete (%I64u of %I64u bytes)", (ULONGLONG)sDownload.m_llDone,
                       sRelease.m_ullSize);
    }
    else if (sDownload.m_omSha256 != sRelease.m_omSha256)
    {
        omError = "the downloaded installer does not match its SHA-256 on GitHub";
    }
    else
    {
        bResult = true;
    }
    if (!bResult)
    {
        ::DeleteFile(omPath);
    }
    return bResult;
}

/* Downloads and checks the installer, closes BUSMASTER-NG and runs the installer. */
static void vInstallUpdate(CWnd* pParent, const SRelease& sRelease)
{
    HWND hParent = pParent->GetSafeHwnd();
    CString omInstaller, omError;
    if (!bDownloadInstaller(hParent, sRelease, omInstaller, omError))
    {
        if (!omError.IsEmpty())
        {
            CString omText;
            omText.Format("The update could not be installed: %s.\n\nOpen the release page to download it by hand?",
                          (LPCTSTR)omError);
            if (::MessageBox(hParent, omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                vOpenUrl(omReleasePage(sRelease));
            }
        }
        return;
    }

    // Close first, with the usual questions about saving the configuration, and only then start the installer:
    // started earlier, it would close BUSMASTER-NG itself and unsaved changes would be lost. This is what
    // CWinApp::OnAppExit does; afterwards pParent no longer exists.
    pParent->SendMessage(WM_CLOSE);
    if (::IsWindow(hParent))
    {
        ::MessageBox(hParent, "The update was not installed, because BUSMASTER-NG stayed open.\n\n"
                     "Use Check for Updates again when you want to install it.", BMNG_PRODUCT_NAME,
                     MB_OK | MB_ICONINFORMATION);
        ::DeleteFile(omInstaller);
        return;
    }

    // closing the main window has posted WM_QUIT, which would end any message box at once: hold it back
    MSG sQuit;
    bool bQuit = ::PeekMessage(&sQuit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;

    CString omArgs;
    omArgs.Format(UPDATE_INSTALLER_ARGS " /LOG=\"%s.log\"", (LPCTSTR)omInstaller);
    SHELLEXECUTEINFO sExecute = { sizeof(sExecute) };
    sExecute.fMask = SEE_MASK_NOASYNC;
    sExecute.lpVerb = "open";
    sExecute.lpFile = omInstaller;
    sExecute.lpParameters = omArgs;
    sExecute.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteEx(&sExecute))
    {
        DWORD dwError = ::GetLastError();
        CString omText;
        if (dwError == ERROR_CANCELLED)
        {
            omText = "The update was cancelled: Windows did not get the permission to install it.\n\n"
                     "Start BUSMASTER-NG again to keep using this version.";
        }
        else
        {
            omText.Format("The installer could not be started, error %lu. It is in\n%s", dwError, (LPCTSTR)omInstaller);
        }
        ::MessageBox(nullptr, omText, BMNG_PRODUCT_NAME, MB_OK | MB_ICONWARNING);
    }

    if (bQuit)
    {
        ::PostQuitMessage((int)sQuit.wParam);
    }
}

enum eUpdateAnswer
{
    UPDATE_ANSWER_INSTALL,
    UPDATE_ANSWER_RELEASE_PAGE,
    UPDATE_ANSWER_NOT_NOW
};

/* Asks whether to install the update now. A task dialog, so that the buttons say what they do in the language of
   BUSMASTER-NG (a message box would show Yes / No / Cancel in the language of Windows). */
static eUpdateAnswer nAskInstallUpdate(HWND hParent, const SRelease& sRelease)
{
    CStringW omInstruction, omContent, omInstall;
    omInstruction.Format(L"%S %S is available", BMNG_PRODUCT_NAME, (LPCSTR)sRelease.m_omVersion);
    omContent.Format(L"This is version %S.", BMNG_VERSION_STRING);
    omInstall.Format(L"Install now\nBUSMASTER-NG downloads the update (%.1f MB), closes and starts again when it "
                     L"is installed.", sRelease.m_ullSize / (1024.0 * 1024.0));
    const TASKDIALOG_BUTTON asButtons[] =
    {
        { UPDATE_ANSWER_INSTALL + 100, omInstall },
        { UPDATE_ANSWER_RELEASE_PAGE + 100, L"See what is new\nOpen the release page in the browser." },
        { UPDATE_ANSWER_NOT_NOW + 100, L"Not now" },
    };
    TASKDIALOGCONFIG sConfig = { sizeof(sConfig) };
    sConfig.hwndParent = hParent;
    sConfig.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    sConfig.pszWindowTitle = L"" BMNG_PRODUCT_NAME;
    sConfig.pszMainIcon = TD_INFORMATION_ICON;
    sConfig.pszMainInstruction = omInstruction;
    sConfig.pszContent = omContent;
    sConfig.cButtons = _countof(asButtons);
    sConfig.pButtons = asButtons;
    sConfig.nDefaultButton = UPDATE_ANSWER_INSTALL + 100;

    // TaskDialogIndirect is in version 6 of the common controls (BUSMASTER.exe.manifest): looked up, not linked
    typedef HRESULT (WINAPI* TASKDIALOGINDIRECT)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE hComCtl = ::GetModuleHandle("comctl32.dll");
    TASKDIALOGINDIRECT pfTaskDialog = hComCtl ? (TASKDIALOGINDIRECT)::GetProcAddress(hComCtl, "TaskDialogIndirect")
                                              : nullptr;
    int nButton = 0;
    if (pfTaskDialog != nullptr && SUCCEEDED(pfTaskDialog(&sConfig, &nButton, nullptr, nullptr)))
    {
        return nButton >= 100 && nButton <= UPDATE_ANSWER_NOT_NOW + 100 ? (eUpdateAnswer)(nButton - 100)
                                                                         : UPDATE_ANSWER_NOT_NOW;
    }

    CString omText;
    omText.Format("BUSMASTER-NG %s is available (this is version %s).\n\nInstall it now? BUSMASTER-NG downloads the "
                  "update (%.1f MB), closes and starts again when it is installed.", (LPCSTR)sRelease.m_omVersion,
                  BMNG_VERSION_STRING, sRelease.m_ullSize / (1024.0 * 1024.0));
    return ::MessageBox(hParent, omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES
           ? UPDATE_ANSWER_INSTALL : UPDATE_ANSWER_NOT_NOW;
}

/* Tells about a newer release and installs it, or opens its page, as the user chooses. */
static void vOfferUpdate(CWnd* pParent, const SRelease& sRelease)
{
    HWND hParent = pParent->GetSafeHwnd();
    CString omText;
    if (!bIsInstalledCopy())
    {
        omText.Format("BUSMASTER-NG %s is available (this is version %s).\n\n"
                      "This copy was not installed with the BUSMASTER-NG installer, so it cannot update itself: "
                      "download the new version from its release page.\n\nOpen the release page?",
                      (LPCSTR)sRelease.m_omVersion, BMNG_VERSION_STRING);
        if (::MessageBox(hParent, omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        {
            vOpenUrl(omReleasePage(sRelease));
        }
    }
    else if (sRelease.m_omInstallerName.IsEmpty() || sRelease.m_omSha256.IsEmpty())
    {
        omText.Format("BUSMASTER-NG %s is available (this is version %s), but it cannot be installed from here: "
                      "%s.\n\nOpen the release page?", (LPCSTR)sRelease.m_omVersion, BMNG_VERSION_STRING,
                      sRelease.m_omInstallerName.IsEmpty() ? "its release has no installer"
                                                           : "GitHub gave no SHA-256 to check the installer");
        if (::MessageBox(hParent, omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        {
            vOpenUrl(omReleasePage(sRelease));
        }
    }
    else
    {
        switch (nAskInstallUpdate(hParent, sRelease))
        {
            case UPDATE_ANSWER_INSTALL:
                vInstallUpdate(pParent, sRelease);
                break;
            case UPDATE_ANSWER_RELEASE_PAGE:
                vOpenUrl(omReleasePage(sRelease));
                break;
            default:
                break;
        }
    }
}

void vCheckForUpdates(CWnd* pParent)
{
    SRelease sRelease;
    CString omError;
    bool bFound;
    {
        CWaitCursor omWait;
        bFound = bGetLatestRelease(sRelease, omError);
    }

    if (!bFound)
    {
        CString omText;
        omText.Format("Could not check for updates: %s.\n\nOpen the BUSMASTER-NG releases page in the browser?",
                      (LPCTSTR)omError);
        if (::MessageBox(pParent->GetSafeHwnd(), omText, BMNG_PRODUCT_NAME, MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            vOpenUrl(UPDATE_RELEASES_URL);
        }
    }
    else if (bIsNewerThanRunning(sRelease))
    {
        vOfferUpdate(pParent, sRelease);
    }
    else
    {
        CString omText;
        omText.Format("BUSMASTER-NG %s is the latest version.", BMNG_VERSION_STRING);
        ::MessageBox(pParent->GetSafeHwnd(), omText, BMNG_PRODUCT_NAME, MB_OK | MB_ICONINFORMATION);
    }
}

// read once: the ribbon asks for the state of its check box very often (user interface thread only)
static int s_nStartupCheck = -1;

bool bIsStartupUpdateCheckOn()
{
    if (s_nStartupCheck < 0)
    {
        DWORD dwValue = 0;
        DWORD dwSize = sizeof(dwValue);
        s_nStartupCheck = ::RegGetValue(HKEY_CURRENT_USER, UPDATE_SETTINGS_KEY, UPDATE_STARTUP_VALUE, RRF_RT_REG_DWORD,
                                        nullptr, &dwValue, &dwSize) == ERROR_SUCCESS && dwValue != 0;
    }
    return s_nStartupCheck != 0;
}

void vSetStartupUpdateCheck(bool bOn)
{
    s_nStartupCheck = bOn ? 1 : 0;
    HKEY hKey = nullptr;
    if (::RegCreateKeyEx(HKEY_CURRENT_USER, UPDATE_SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr)
            == ERROR_SUCCESS)
    {
        DWORD dwValue = bOn ? 1 : 0;
        ::RegSetValueEx(hKey, UPDATE_STARTUP_VALUE, 0, REG_DWORD, (const BYTE*)&dwValue, sizeof(dwValue));
        ::RegCloseKey(hKey);
    }
}

static DWORD WINAPI dwStartupCheckThread(LPVOID pvMainFrame)
{
    SRelease* psRelease = new SRelease;
    CString omError;
    if (bGetLatestRelease(*psRelease, omError) && bIsNewerThanRunning(*psRelease) &&
            ::PostMessage((HWND)pvMainFrame, WM_BMNG_UPDATE_FOUND, 0, (LPARAM)psRelease))
    {
        return 0;   // the main frame deletes it
    }
    delete psRelease;
    return 0;
}

void vStartStartupUpdateCheck(HWND hMainFrame)
{
    if (bIsStartupUpdateCheckOn())
    {
        HANDLE hThread = ::CreateThread(nullptr, 0, dwStartupCheckThread, hMainFrame, 0, nullptr);
        if (hThread != nullptr)
        {
            ::CloseHandle(hThread);
        }
    }
}

void vOnUpdateFound(CWnd* pParent, LPARAM lParam)
{
    SRelease* psRelease = (SRelease*)lParam;
    SRelease sRelease = *psRelease;
    delete psRelease;
    vOfferUpdate(pParent, sRelease);
}
