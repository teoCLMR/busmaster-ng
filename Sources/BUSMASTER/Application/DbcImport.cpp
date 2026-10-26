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
 * \file      DbcImport.cpp
 * \brief     BUSMASTER-NG: loading of CAN databases in DBC format (rbei-etas/busmaster issue 586)
 */
#include "stdafx.h"
#include <shlobj.h>
#include "Utility/MultiLanguageSupport.h"
#include "ProtocolsDefinitions.h"
#include "DbcImport.h"

/* Interface of the DBC converter, declared exactly as in Format Converter/DBC2DBFConverterLibrary/DBCConverterBase.h
   (same order, so the same virtual table). The header itself is not included: it declares classes exported by the
   converter DLL, which would link BUSMASTER.exe to that DLL, and the DLL is loaded only when a DBC is loaded. */
class CMessage;
class CDBCConverterBase
{
public:
    virtual HRESULT LoadDBCFile(CString strDBCFile) = 0;
    virtual HRESULT GenerateImportList(/* sMESSAGE*& */) = 0;
    virtual HRESULT ConvertFile(CString strDBFFile) = 0;
    virtual HRESULT FindMessage(CString strMsgName, CMessage* ) = 0;
    virtual HRESULT FindMessage(UINT unMsgId, CMessage* ) = 0;
    virtual HRESULT GetResultString(char* pchResult) = 0;
    virtual HRESULT FindSignalAlias(CString& strMsgName, CString& strSignalName, CString& strSignalAlias) = 0 ;
    virtual HRESULT ClearMsgList() = 0;
    virtual HRESULT GetMessageNameList(CStringArray& meassageList) = 0;
    virtual ~CDBCConverterBase() {};
};
typedef HRESULT(*GETCONVERTER)(CDBCConverterBase*& ouDBCConverter, ETYPE_BUS);

// result codes of CDBFConverter (DBC2DBFConverterLibrary/DBFConverter.h)
#define DBC_RC_NOERROR                  0
#define DBC_RC_COMPLETED_WITH_WARNINGS  1
#define DBC_RC_FILEOPEN_ERROR_OUTFILE   3
#define DBC_RC_FORMAT_ERROR_INFILE      4

#define DBC_CONVERTER_DLL   "ConverterPlugins\\DBC2DBFConverterLibrary.dll"
#define DBC_SOURCE_MARKER   "//Converted from %s by BUSMASTER-NG"

static CString omGetExeFolder()
{
    char acPath[MAX_PATH] = { 0 };
    ::GetModuleFileName(nullptr, acPath, MAX_PATH);
    CString omFolder = acPath;
    int nSlash = omFolder.ReverseFind('\\');
    return (nSlash > 0) ? omFolder.Left(nSlash) : omFolder;
}

static CString omGetFileName(const CString& omPath)
{
    int nSlash = omPath.ReverseFind('\\');
    return (nSlash >= 0) ? omPath.Mid(nSlash + 1) : omPath;
}

static CString omReplaceExtension(const CString& omPath, const char* pszExtension)
{
    int nDot = omPath.ReverseFind('.');
    int nSlash = omPath.ReverseFind('\\');
    CString omBase = (nDot > nSlash) ? omPath.Left(nDot) : omPath;
    return omBase + pszExtension;
}

/* True if the database was written by the DBC converter from a DBC with this file name */
static bool bIsConvertedFrom(const CString& omDbfFile, const CString& omDbcName)
{
    CStdioFile omFile;
    if (!omFile.Open(omDbfFile, CFile::modeRead | CFile::shareDenyNone | CFile::typeText))
    {
        return false;
    }
    CString omMarker;
    omMarker.Format(DBC_SOURCE_MARKER, (LPCTSTR)omDbcName);
    CString omLine;
    for (int nLine = 0; (nLine < 3) && omFile.ReadString(omLine); nLine++)
    {
        if (0 == omLine.CompareNoCase(omMarker))
        {
            return true;
        }
    }
    return false;
}

/* Folder for databases converted from DBC files in folders that cannot be written (for example a network share) */
static CString omGetFallbackFolder()
{
    char acPath[MAX_PATH] = { 0 };
    CString omFolder;
    if (SUCCEEDED(::SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, acPath)))
    {
        omFolder = acPath;
        omFolder += "\\BUSMASTER-NG";
        ::CreateDirectory(omFolder, nullptr);
        omFolder += "\\DBC";
        ::CreateDirectory(omFolder, nullptr);
    }
    return omFolder;
}

static void vReadConversionLog(const CString& omDbfFile, CStringArray& omWarnings)
{
    CStdioFile omFile;
    if (!omFile.Open(omReplaceExtension(omDbfFile, ".log"), CFile::modeRead | CFile::shareDenyNone | CFile::typeText))
    {
        return;
    }
    CString omLine;
    while (omFile.ReadString(omLine))
    {
        omLine.Replace('\t', ' ');
        omLine.Trim();
        if (!omLine.IsEmpty() && (omLine != "Conversion Warnings Log"))
        {
            omWarnings.Add(omLine);
        }
    }
}

eDbcImportResult eConvertDbcToDbf(HWND hParent, const CString& omDbcFile, CString& omDbfFile,
                                  CString& omMessage, CStringArray& omWarnings)
{
    static GETCONVERTER s_pfGetConverter = nullptr;
    CString omDllPath = omGetExeFolder() + "\\" DBC_CONVERTER_DLL;

    if (nullptr == s_pfGetConverter)
    {
        // kept loaded: the converter is an MFC extension DLL linked into the resource chain
        HMODULE hModule = ::LoadLibrary(omDllPath);
        if (nullptr != hModule)
        {
            s_pfGetConverter = (GETCONVERTER)::GetProcAddress(hModule, "GetDBCConverter");
        }
    }
    if (nullptr == s_pfGetConverter)
    {
        omMessage.Format(_("The DBC converter %s is missing or damaged. Reinstall BUSMASTER-NG."), (LPCTSTR)omDllPath);
        return DBC_IMPORT_FAILED;
    }

    CString omDbcName = omGetFileName(omDbcFile);
    omDbfFile = omReplaceExtension(omDbcFile, ".dbf");

    // an existing database is never replaced without asking: it may have been changed in the database editor
    if (INVALID_FILE_ATTRIBUTES != ::GetFileAttributes(omDbfFile))
    {
        CString omOrigin;
        if (bIsConvertedFrom(omDbfFile, omDbcName))
        {
            CFileStatus ouDbcStatus;
            CFileStatus ouDbfStatus;
            bool bDbcIsNewer = CFile::GetStatus(omDbcFile, ouDbcStatus) && CFile::GetStatus(omDbfFile, ouDbfStatus) &&
                               (ouDbcStatus.m_mtime > ouDbfStatus.m_mtime);
            omOrigin = bDbcIsNewer ? _("It was created from this DBC, and the DBC has been changed since then.")
                                   : _("It was created from this DBC, and the DBC has not been changed since then.");
        }
        else
        {
            omOrigin = _("It was not created from this DBC, so it may contain changes made by hand.");
        }
        CString omQuestion;
        omQuestion.Format(_("Next to %s there is already a BUSMASTER database with the same name:\n%s\n\n"
                            "%s\n\n"
                            "Yes: replace it with a new conversion of the DBC\n"
                            "No: associate the existing database\n"
                            "Cancel: skip this DBC"),
                          (LPCTSTR)omDbcName, (LPCTSTR)omDbfFile, (LPCTSTR)omOrigin);
        int nAnswer = ::MessageBox(hParent, omQuestion, "BUSMASTER-NG", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (IDNO == nAnswer)
        {
            omMessage = _("The existing BUSMASTER database is associated, the DBC is not converted.");
            return DBC_IMPORT_OK;
        }
        if (IDYES != nAnswer)
        {
            return DBC_IMPORT_SKIPPED;
        }
    }

    CDBCConverterBase* pouConverter = nullptr;
    s_pfGetConverter(pouConverter, CAN);
    if (nullptr == pouConverter)
    {
        omMessage = _("The DBC converter could not be started.");
        return DBC_IMPORT_FAILED;
    }

    eDbcImportResult eResult = DBC_IMPORT_FAILED;
    char acResult[1024] = { 0 };
    pouConverter->ClearMsgList();
    HRESULT hResult = pouConverter->LoadDBCFile(omDbcFile);
    if (S_OK == hResult)
    {
        hResult = pouConverter->ConvertFile(omDbfFile);
        if (DBC_RC_FILEOPEN_ERROR_OUTFILE == hResult)
        {
            // the folder of the DBC is read-only: use the local application data
            CString omFolder = omGetFallbackFolder();
            if (!omFolder.IsEmpty())
            {
                omDbfFile = omFolder + "\\" + omReplaceExtension(omDbcName, ".dbf");
                hResult = pouConverter->ConvertFile(omDbfFile);
            }
        }
    }
    pouConverter->GetResultString(acResult);
    omMessage = acResult;
    if (DBC_RC_FORMAT_ERROR_INFILE == hResult)
    {
        omMessage += " ";
        omMessage += _("No CAN message with up to 8 data bytes was found in the DBC.");
    }

    if (DBC_RC_NOERROR == hResult)
    {
        eResult = DBC_IMPORT_OK;
    }
    else if (DBC_RC_COMPLETED_WITH_WARNINGS == hResult)
    {
        vReadConversionLog(omDbfFile, omWarnings);
        eResult = DBC_IMPORT_WARNINGS;
    }
    delete pouConverter;
    return eResult;
}
