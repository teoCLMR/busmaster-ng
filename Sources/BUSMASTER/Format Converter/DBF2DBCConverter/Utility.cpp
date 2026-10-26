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
 * \file      Utility.cpp
 * \brief     Defines Global Utility functions
 * \authors   Venkatanarayana Makam, Tobias Lorenz
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Defines Global Utility functions
 */

/* Project includes */
#include "Utility.h"

/**
 * \brief      Converts the double physical values to unsigned long long int values
 * \param[in]  dMaxVal Maximum physical value
 * \param[in]  dMinVal Minimum physical value
 * \param[in]  dOffset Offset value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] unMaxVal Maximum vlaue
 * \param[out] unMinVal Minimum value
 * \return     Return code
 *
 * Converts the double physical values to unsigned long long int values
 */
unsigned int unConvertPhysicalToRaw(double dMaxVal, double dMinVal, double dOffset, double dScaleFactor, unsigned long long int& unMaxVal, unsigned long long int& unMinVal)
{
    double dTempMaxVal = ((dMaxVal - dOffset) / dScaleFactor);
    double dTempMinVal = ((dMinVal - dOffset) / dScaleFactor);
    unMaxVal = (unsigned long long int)unRoundOffdoubleValue(dTempMaxVal);
    unMinVal = (unsigned long long int)unRoundOffdoubleValue(dTempMinVal);
    return 0;
}

/**
 * \brief      Converts the double physical values to long long int values
 * \param[in]  dMaxVal Maximum physical value
 * \param[in]  dMinVal Minimum physical value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] nMaxVal Maximum value
 * \param[out] nMinVal Minimum value
 * \return     Status code
 *
 * Converts the double physical values to long long int values
 */
int nConvertPhysicalToRaw(double dMaxVal, double dMinVal, double dOffset, double dScaleFactor, long long int& nMaxVal, long long int& nMinVal)
{
    double dTempMaxVal = ((dMaxVal - dOffset) / dScaleFactor);
    double dTempMinVal = ((dMinVal - dOffset) / dScaleFactor);
    nMaxVal = (long long int)unRoundOffdoubleValue(dTempMaxVal);
    nMinVal = (long long int)unRoundOffdoubleValue(dTempMinVal);
    return 0;
}

/**
 * \brief      Converts the double physical values to long long int values
 * \param[in]  unMaxRawVal Maximum raw value
 * \param[in]  unMinRawVal Minimum raw value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] dMaxPhyVal Maximum physical vlaue
 * \param[out] dMinPhyVal Minimum physical vlaue
 * \return     Status code
 *
 * Converts the double physical values to long long int values.
 */
unsigned int unConvertRawToPhysical(unsigned long long int unMaxRawVal, unsigned long long int unMinRawVal, double dOffset, double dScaleFactor, double& dMaxPhyVal, double& dMinPhyVal)
{
    //Maximum value
    dMaxPhyVal = static_cast<double>(unMaxRawVal);
    dMaxPhyVal *= dScaleFactor;
    dMaxPhyVal += dOffset;
    //Minimum value
    dMinPhyVal = static_cast<double>(unMinRawVal);
    dMinPhyVal *= dScaleFactor;
    dMinPhyVal += dOffset;
    return 0;
}

/**
 * \brief      Converts the double physical values to long long int raw values
 * \param[in]  unMaxRawVal Maximum raw value
 * \param[in]  unMinRawVal Minimum raw value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] dMaxPhyVal Maximum physical vlaue
 * \param[out] dMinPhyVal Minimum physical vlaue
 * \return     Status code
 *
 * Converts the double physical values to long long int raw values.
 */
int nConvertRawToPhysical(long long int unMaxRawVal, long long int unMinRawVal, double dOffset, double dScaleFactor, double& dMaxPhyVal, double& dMinPhyVal)
{
    //Maximum value
    dMaxPhyVal = static_cast<double>(unMaxRawVal);
    dMaxPhyVal *= dScaleFactor;
    dMaxPhyVal += dOffset;
    //Minimum value
    dMinPhyVal = static_cast<double>(unMinRawVal);
    dMinPhyVal *= dScaleFactor;
    dMinPhyVal += dOffset;
    return 0;
}

/**
 * \brief      This function will convert the double value to unsigned long long int.
 * \param[in]  dValue double Value
 * \return     unsigned long long int Value
 *
 * This function will convert the double value to unsigned long long int.
 * If the double value precision is more than 0.5 the unit value will be added 1.
 */
unsigned long long int unRoundOffdoubleValue(double dValue)
{
    unsigned long long int unVal = (unsigned long long int)dValue;
    double dTempVal = dValue - (double)unVal;

    if(dTempVal >= 0.5)
    {
        unVal++;
    }

    return unVal;
}

/* BUSMASTER-NG: the old code read factor and offset with the decimal symbol of Windows into floats and wrote them
   with 6 digits, so values changed on every DBC -> DBF -> DBC round trip and on Italian or German PCs. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

static _locale_t ouGetCLocale()
{
    static _locale_t s_ouLocale = _create_locale(LC_NUMERIC, "C");
    return s_ouLocale;
}

double dParseDbfDouble(const char* pcText)
{
    if (nullptr == pcText)
    {
        return 0.0;
    }
    return _strtod_l(pcText, nullptr, ouGetCLocale());
}

std::string strFormatDbcDouble(double dValue, bool bRoundTo15Digits)
{
    if ((dValue != dValue) || (dValue == 0.0))
    {
        return "0";     // NaN or -0
    }
    char acText[64];
    if (bRoundTo15Digits)
    {
        // computed values (for example max = raw max * factor + offset): 490.21993519999995 -> 490.2199352
        _sprintf_s_l(acText, sizeof(acText), "%.15g", ouGetCLocale(), dValue);
        dValue = _strtod_l(acText, nullptr, ouGetCLocale());
    }
    double dAbs = (dValue < 0.0) ? -dValue : dValue;
    if ((dAbs >= 1e-7) && (dAbs < 1e16))
    {
        // plain notation with the fewest decimals that read back exactly (1 rather than 1e+00)
        for (int nDecimals = 0; nDecimals <= 24; nDecimals++)
        {
            _sprintf_s_l(acText, sizeof(acText), "%.*f", ouGetCLocale(), nDecimals, dValue);
            if (_strtod_l(acText, nullptr, ouGetCLocale()) == dValue)
            {
                return acText;
            }
        }
    }
    for (int nDigits = 1; nDigits <= 17; nDigits++)
    {
        _sprintf_s_l(acText, sizeof(acText), "%.*g", ouGetCLocale(), nDigits, dValue);
        if (_strtod_l(acText, nullptr, ouGetCLocale()) == dValue)
        {
            break;
        }
    }
    return acText;
}
