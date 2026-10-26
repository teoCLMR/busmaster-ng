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
 * \brief     Implementation of global utility functions
 * \authors   Venkatanarayana Makam, Tobias Lorenz
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Implementation of global utility functions.
 */

/* Project includes */
#include "Utility.h"

/**
 * \brief      Converts the DOUBLE physical values to UINT64 RAW values
 * \param[in]  dMaxVal Maximum physical value
 * \param[in]  dMinVal Minimum physical value
 * \param[in]  dOffset Offset value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] unMaxVal Maximum RAW value
 * \param[out] unMinVal Minimum RAW value
 *
 * Converts the DOUBLE physical values to UINT64 RAW values.
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
 * \brief      Converts the DOUBLE physical values to INT64 RAW values
 * \param[in]  dMaxVal Maximum physical value
 * \param[in]  dMinVal Minimum physical value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] nMaxVal Maximum RAW value
 * \param[out] nMinVal Minimum RAW value
 *
 * Converts the DOUBLE physical values to INT64 RAW values.
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
 * \brief      Converts the DOUBLE physical values to INT64 RAW values
 * \param[in]  unMaxRawVal Maximum raw value
 * \param[in]  unMinRawVal Minimum raw value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] dMaxPhyVal Maximum physical vlaue
 * \param[out] dMinPhyVal Minimum physical vlaue
 *
 * Converts the DOUBLE physical values to INT64 RAW values.
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
 * \brief      Converts the DOUBLE physical values to INT64 RAW values
 * \param[in]  unMaxRawVal Maximum raw value
 * \param[in]  unMinRawVal Minimum raw value
 * \param[in]  dOffset Offset Value
 * \param[in]  dScaleFactor Scaling Factor
 * \param[out] dMaxPhyVal Maximum physical value
 * \param[out] dMinPhyVal Minimum physical value
 *
 * Converts the DOUBLE physical values to INT64 RAW values.
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
 * \brief      Convert the double value to UINT64.
 * \param[in]  dValue double value
 * \return     value as UINT64
 *
 * This function will convert the double value to UINT64.
 * If the double value precision is more than 0.5 the unit value will be added 1.
 */
unsigned long long int unRoundOffdoubleValue(double dValue)
{
    unsigned long long int unVal = (unsigned long long int)round(dValue);
    /*double dTempVal = dValue - (double)unVal;

    if(dTempVal >= 0.5)
    {
        unVal++;
    }*/

    return unVal;
}
void strTrimRight(string& str, string strSub)
{
    size_t endpos = str.find_last_not_of(strSub);
    if( string::npos != endpos )
    {
        str = str.substr( 0, endpos+1 );
    }
}
void strTrimLeft(string& str, string strSub)
{
    // trim leading spaces
    size_t startpos = str.find_first_not_of(strSub);
    if( string::npos != startpos )
    {
        str = str.substr( startpos );
    }
}
/* BUSMASTER-NG: number and text helpers. The old code read numbers with the language settings of Windows
   (on an Italian or German PC "0.5" became 0 or 0,5 became 0) and wrote them with 6 significant digits
   (offset -2105540607 became -2.10554e+09), see rbei-etas/busmaster issues 378, 498, 499, 570, 932, 1248, 1308. */
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static _locale_t ouGetCLocale()
{
    static _locale_t s_ouLocale = _create_locale(LC_NUMERIC, "C");
    return s_ouLocale;
}

double dParseDbcDouble(const char* pcText)
{
    if (nullptr == pcText)
    {
        return 0.0;
    }
    return _strtod_l(pcText, nullptr, ouGetCLocale());
}

long long int nParseDbcInt64(const char* pcText)
{
    if (nullptr == pcText)
    {
        return 0;
    }
    char* pcEnd = nullptr;
    long long int nValue = _strtoi64(pcText, &pcEnd, 10);
    if ((nullptr != pcEnd) && ((*pcEnd == '.') || (*pcEnd == 'e') || (*pcEnd == 'E')))
    {
        nValue = (long long int)llround(dParseDbcDouble(pcText));
    }
    return nValue;
}

unsigned long long int unParseDbcUInt64(const char* pcText)
{
    if (nullptr == pcText)
    {
        return 0;
    }
    char* pcEnd = nullptr;
    unsigned long long int unValue = _strtoui64(pcText, &pcEnd, 10);
    if ((nullptr != pcEnd) && ((*pcEnd == '.') || (*pcEnd == 'e') || (*pcEnd == 'E')))
    {
        unValue = (unsigned long long int)llround(dParseDbcDouble(pcText));
    }
    return unValue;
}

string strFormatDbfDouble(double dValue)
{
    if ((dValue != dValue) || (dValue == 0.0))
    {
        return "0";     // NaN or -0
    }
    char acText[400];
    for (int nDecimals = 0; nDecimals <= 30; nDecimals++)
    {
        _sprintf_s_l(acText, sizeof(acText), "%.*f", ouGetCLocale(), nDecimals, dValue);
        if (_strtod_l(acText, nullptr, ouGetCLocale()) == dValue)
        {
            return acText;
        }
    }
    _sprintf_s_l(acText, sizeof(acText), "%.17g", ouGetCLocale(), dValue);
    return acText;
}

bool bReadDbcLine(fstream& fileInput, vector<char>& acLine)
{
    string strLine;
    if (!getline(fileInput, strLine))
    {
        acLine.assign(1, '\0');
        return false;
    }
    while (!strLine.empty() && ((strLine.back() == '\r') || (strLine.back() == '\n')))
    {
        strLine.pop_back();
    }
    for (size_t nIndex = 0; nIndex < strLine.size(); nIndex++)
    {
        if (strLine[nIndex] == '\t')
        {
            strLine[nIndex] = ' ';
        }
    }
    acLine.assign(strLine.begin(), strLine.end());
    acLine.push_back('\0');
    return true;
}

bool bIsDbcStatementComplete(const string& strStatement)
{
    bool bInQuotes = false;
    for (size_t nIndex = 0; nIndex < strStatement.size(); nIndex++)
    {
        char ch = strStatement[nIndex];
        if (bInQuotes && (ch == '\\') && (nIndex + 1 < strStatement.size()))
        {
            nIndex++;   // escaped character, for example \"
        }
        else if (ch == '"')
        {
            bInQuotes = !bInQuotes;
        }
        else if (!bInQuotes && (ch == ';'))
        {
            return true;
        }
    }
    return false;
}

void vReadDbcStatement(fstream& fileInput, string& strStatement)
{
    vector<char> acLine;
    while (!bIsDbcStatementComplete(strStatement))
    {
        if (!bReadDbcLine(fileInput, acLine))
        {
            break;
        }
        strStatement += " ";
        strStatement += &acLine[0];
    }
}

void vParseDbcValueDescriptions(const string& strText, list< pair<string, string> >& listValues)
{
    size_t nIndex = 0;
    size_t nSize = strText.size();
    for (;;)
    {
        while ((nIndex < nSize) && (strText[nIndex] == ' '))
        {
            nIndex++;
        }
        if ((nIndex >= nSize) || (strText[nIndex] == ';'))
        {
            break;
        }
        string strValue;
        while ((nIndex < nSize) && (strText[nIndex] != ' ') && (strText[nIndex] != '"') && (strText[nIndex] != ';'))
        {
            strValue += strText[nIndex++];
        }
        while ((nIndex < nSize) && (strText[nIndex] == ' '))
        {
            nIndex++;
        }
        if ((nIndex >= nSize) || (strText[nIndex] != '"'))
        {
            break;      // malformed: a value without description
        }
        nIndex++;       // opening quote
        string strDesc;
        while ((nIndex < nSize) && (strText[nIndex] != '"'))
        {
            if ((strText[nIndex] == '\\') && (nIndex + 1 < nSize))
            {
                nIndex++;
            }
            strDesc += strText[nIndex++];
        }
        nIndex++;       // closing quote
        listValues.push_back(make_pair(strValue, strDesc));
    }
}
