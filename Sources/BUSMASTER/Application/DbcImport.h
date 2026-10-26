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
 * \file      DbcImport.h
 * \brief     BUSMASTER-NG: loading of CAN databases in DBC format
 *
 * A DBC file is converted to a BUSMASTER database (.dbf) next to it, with the DBC converter of the
 * Format Converter (ConverterPlugins\DBC2DBFConverterLibrary.dll); the .dbf is then associated as usual,
 * so configurations stay readable by BUSMASTER 3.2.x.
 */
#pragma once

enum eDbcImportResult
{
    DBC_IMPORT_OK,          // converted (or the existing database was chosen)
    DBC_IMPORT_WARNINGS,    // converted, some signals or messages need attention (see omWarnings)
    DBC_IMPORT_FAILED,      // not converted, omMessage says why
    DBC_IMPORT_SKIPPED      // the user chose to skip this file
};

/**
 * Converts omDbcFile to a BUSMASTER database. omDbfFile receives the database to associate: <name>.dbf next to the
 * DBC or, when that folder cannot be written, the same name in the BUSMASTER-NG folder of the local application
 * data. An existing <name>.dbf is replaced only if the user agrees; the question says whether it was created from
 * this DBC and whether the DBC changed since then.
 */
eDbcImportResult eConvertDbcToDbf(HWND hParent, const CString& omDbcFile, CString& omDbfFile,
                                  CString& omMessage, CStringArray& omWarnings);
