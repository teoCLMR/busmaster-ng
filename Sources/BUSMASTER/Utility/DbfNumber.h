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
 * \file      DbfNumber.h
 * \brief     BUSMASTER-NG: decimal numbers of the CAN database (.dbf) as text
 *
 * Factor and offset are written with '.' as decimal symbol whatever the Windows settings, in fixed notation
 * (DBManager.dll does not read exponents) and with the fewest decimals that give back the same value, so a
 * factor such as 0.0000001 is no longer written as 0.000000. Same rules as the DBC to DBF converter.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string>

namespace DbfNumber
{
    inline _locale_t hCLocale()
    {
        static _locale_t s_hLocale = _create_locale(LC_NUMERIC, "C");
        return s_hLocale;
    }

    inline std::string strFormat(double dValue)
    {
        if ((dValue != dValue) || (dValue == 0.0))
        {
            return "0";     // NaN or -0
        }
        char acText[400];
        for (int nDecimals = 0; nDecimals <= 30; nDecimals++)
        {
            _sprintf_s_l(acText, sizeof(acText), "%.*f", hCLocale(), nDecimals, dValue);
            if (_strtod_l(acText, nullptr, hCLocale()) == dValue)
            {
                return acText;
            }
        }
        _sprintf_s_l(acText, sizeof(acText), "%.17g", hCLocale(), dValue);
        return acText;
    }

    // Reads '.' or ',' as decimal symbol (files and edit boxes written with the Windows settings of the old versions)
    inline double dParse(const char* pcText)
    {
        std::string strText = (pcText != nullptr) ? pcText : "";
        for (size_t nIndex = 0; nIndex < strText.size(); nIndex++)
        {
            if (strText[nIndex] == ',')
            {
                strText[nIndex] = '.';
            }
        }
        return _strtod_l(strText.c_str(), nullptr, hCLocale());
    }
}
