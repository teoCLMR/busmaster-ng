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
 * \file      ValueDescriptor.cpp
 * \brief     Implementation of the CValueDescriptor class.
 * \authors   Mahesh.B.S, Tobias Lorenz
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Implementation of the CValueDescriptor class.
 */

/* Project includes */
#include "Converter.h"
#include "Signal.h"
#include "ValueDescriptor.h"
#include <algorithm>

using namespace std;

/**
 * \brief Constructor
 *
 * Constructor of CValueDescriptor
 */
CValueDescriptor::CValueDescriptor()
{
    m_value.iValue = 1;
    m_value.uiValue = 1;
    m_value.fValue = 1;
    m_value.dValue = 1;
    m_value.cValue = "";
    m_value.i64Value = 1;
    m_value.ui64Value = 1;
}

/**
 * \brief Destructor
 *
 * Destructor of CValueDescriptor
 */
CValueDescriptor::~CValueDescriptor()
{
}

/**
 * \brief      Extracts the Value Descriptor pair from the given Line
 * \param[in]  pcLine The given line
 * \return     Status code
 *
 * Extracts the Value Descriptor pair from the given Line
 * and creates a list of the same
 */
int CValueDescriptor::Format(char* pcLine)
{
    // BUSMASTER-NG: [VALUE_DESCRIPTION] "<description>",<value>  or, in old databases, <description>,<value>
    // without quotes. The old code expected the quotes: without them it took the whole text as description and 0 as
    // value (issue 1178), and it copied into 256-byte buffers.
    string strLine = pcLine;
    while (!strLine.empty() && ((strLine.back() == '\r') || (strLine.back() == '\n') || (strLine.back() == ' ')))
    {
        strLine.pop_back();
    }
    size_t nStart = strLine.find_first_not_of(' ');
    if (string::npos == nStart)
    {
        nStart = strLine.size();
    }
    strLine = strLine.substr(nStart);

    string strDesc;
    string strValue;
    if (!strLine.empty() && (strLine[0] == '"'))
    {
        size_t nClose = strLine.find('"', 1);
        if (string::npos == nClose)
        {
            nClose = strLine.size();
        }
        strDesc = strLine.substr(1, nClose - 1);
        size_t nComma = strLine.find(',', nClose);
        strValue = (string::npos == nComma) ? "" : strLine.substr(nComma + 1);
    }
    else
    {
        size_t nComma = strLine.rfind(',');
        strDesc = (string::npos == nComma) ? strLine : strLine.substr(0, nComma);
        strValue = (string::npos == nComma) ? "" : strLine.substr(nComma + 1);
    }

    m_value.i64Value = _atoi64(strValue.c_str());
    // a description between quotes in the DBC must not contain quotes
    replace(strDesc.begin(), strDesc.end(), '"', '\'');
    m_sDescriptor = strDesc;
    return 1;
}

/**
 * \brief     Writes the Value Descriptor in the CANoe format
 * \param[in] fileOutput Output file
 * \param[in] m_ucType Value Descriptor Type
 * \param[in] m_listValueDescriptor List of Value Descriptors
 *
 * Writes the Value Descriptor in the CANoe format in the output file
 */
void CValueDescriptor::writeValueDescToFile(fstream& fileOutput,char m_ucType,list<CValueDescriptor>& m_listValueDescriptor)
{
    list<CValueDescriptor>::iterator rValDesc;

    for(rValDesc=m_listValueDescriptor.begin(); rValDesc!=m_listValueDescriptor.end(); ++rValDesc)
    {
        switch(m_ucType)
        {
            case CSignal::SIG_TYPE_INT:
            case CSignal::SIG_TYPE_FLOAT:
            case CSignal::SIG_TYPE_DOUBLE:
            case CSignal::SIG_TYPE_INT64:
                fileOutput << dec << rValDesc->m_value.i64Value;
                fileOutput << " \"" << rValDesc->m_sDescriptor.c_str() << "\" ";
                break;

            case CSignal::SIG_TYPE_BOOL:
            case CSignal::SIG_TYPE_UINT:
            case CSignal::SIG_TYPE_UINT64:
                fileOutput << dec << rValDesc->m_value.ui64Value;
                fileOutput << " \"" << rValDesc->m_sDescriptor.c_str() << "\" ";
                break;

            default:
                break;
        }
    }

    fileOutput << ";" << endl;
    return;
}
