/*
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * \file      LastFolder.h
 * \brief     BUSMASTER-NG: last folder of each kind of file in the Open and Save dialogs
 *
 * Without an initial folder, Windows opens every file dialog of BUSMASTER in the same last folder, so after
 * importing a DBC the next "Load configuration" starts in the DBC folder. Each kind of file (configuration, DBC,
 * database, log, ...) now has its own last folder, kept under HKEY_CURRENT_USER\Software\BUSMASTER-NG\Last folders
 * (not under the version key, so an update does not forget them).
 *
 * Use:
 *     LastFolder::vPrepare(omFileDlg, LastFolder::CONFIGURATION);
 *     if (omFileDlg.DoModal() == IDOK)
 *     {
 *         LastFolder::vRemember(LastFolder::CONFIGURATION, omFileDlg.GetPathName());
 */

#pragma once

#include <afxdlgs.h>

namespace LastFolder
{
    // Kinds of file (registry value names)
    static const char* const CONFIGURATION = "Configuration";
    static const char* const DBC = "DBC";
    static const char* const DATABASE = "Database";
    static const char* const DATABASE_J1939 = "Database J1939";
    static const char* const LDF = "LDF";
    static const char* const LOG = "Log";
    static const char* const NODE_SOURCE = "Node source";
    static const char* const NODE_HEADER = "Node header";
    static const char* const GRAPH_EXPORT = "Graph export";
    static const char* const TEST_SETUP = "Test setup";

    static const char* const s_pcKey = "Software\\BUSMASTER-NG\\Last folders";

    inline CString strGet(const char* pcKind)
    {
        char acFolder[MAX_PATH * 2] = { 0 };
        DWORD dwSize = sizeof(acFolder);
        if (ERROR_SUCCESS != ::RegGetValueA(HKEY_CURRENT_USER, s_pcKey, pcKind, RRF_RT_REG_SZ, nullptr, acFolder,
                                            &dwSize))
        {
            return "";
        }
        // a folder that no longer exists (USB stick, renamed project) is ignored
        DWORD dwAttributes = ::GetFileAttributesA(acFolder);
        if ((dwAttributes == INVALID_FILE_ATTRIBUTES) || !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            return "";
        }
        return acFolder;
    }

    // Folder of a chosen file (or the folder itself when a folder is given)
    inline void vRemember(const char* pcKind, const CString& omPath)
    {
        CString omFolder = omPath;
        DWORD dwAttributes = ::GetFileAttributesA(omFolder);
        if ((dwAttributes == INVALID_FILE_ATTRIBUTES) || !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            int nSlash = omFolder.ReverseFind('\\');
            if (nSlash <= 0)
            {
                return;
            }
            omFolder = omFolder.Left(nSlash);
            if (omFolder.GetLength() == 2)
            {
                omFolder += '\\';   // "C:" is the current folder of drive C, "C:\" its root
            }
        }
        ::RegSetKeyValueA(HKEY_CURRENT_USER, s_pcKey, pcKind, REG_SZ, (LPCSTR)omFolder, omFolder.GetLength() + 1);
    }

    // Starts the dialog in the last folder of this kind, unless its file name already has a folder (the file in
    // use, which wins over lpstrInitialDir anyway). Call it just before DoModal: the text is kept until the next call.
    inline void vPrepare(CFileDialog& omDlg, const char* pcKind)
    {
        static CString s_omInitialDir;
        LPCSTR pcFile = omDlg.m_ofn.lpstrFile;
        if ((pcFile != nullptr) && (strchr(pcFile, '\\') != nullptr))
        {
            return;
        }
        s_omInitialDir = strGet(pcKind);
        if (!s_omInitialDir.IsEmpty())
        {
            omDlg.m_ofn.lpstrInitialDir = s_omInitialDir;
        }
    }

    // With OFN_ALLOWMULTISELECT: the folder of the first selected file
    inline void vRememberFirst(const char* pcKind, CFileDialog& omDlg)
    {
        POSITION pos = omDlg.GetStartPosition();
        if (pos != nullptr)
        {
            vRemember(pcKind, omDlg.GetNextPathName(pos));
        }
    }
}
