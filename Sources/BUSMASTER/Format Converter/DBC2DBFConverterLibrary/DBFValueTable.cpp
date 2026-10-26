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
 * \file      ValueTable.cpp
 * \brief     Implementation of the value table class
 * \authors   Mahesh.B.S, Tobias Lorenz
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Implementation of the value table class.
 */

/* Project includes */
#include "DBFSignal.h"
#include "DBFTagDefinitions.h"
#include "DBFValueTable.h"
#include "Utility.h"
#include <algorithm>

using namespace std;

/**
 * \brief Constructor
 *
 * Constructor of CValueTable
 */
CValueTable::CValueTable()
{
    m_TableName = "";
}

/**
 * \brief Destructor
 *
 * Destructor of CValueTable
 */
CValueTable::~CValueTable()
{
}

/**
 * \brief     = operator overloading
 * \param[in] Tab Data to assign
 *
 * Copyies the contents of Tab to the local instance.
 */
CValueTable& CValueTable::operator=(const CValueTable& Tab)
{
    m_TableName = Tab.m_TableName;
    m_values = Tab.m_values;
    return (*this);
}

/**
 * \brief     Extracts Values and value descriptors from the line
 * \param[in] pcLine String having the value table name followed by the
 *            value, value descriptor as in canoe format
 * \param[in] fileInput Input file
 *
 * Extracts Values and value descriptors from the line.
 */
int CValueTable::Format(char* pcLine, fstream& fileInput)
{
    // BUSMASTER-NG: the old code copied names and descriptions into 65-byte buffers (a longer description
    // overwrote the stack) and joined continuation lines into a 4-byte copy.
    string strStatement = pcLine;
    vReadDbcStatement(fileInput, strStatement);

    // table name
    size_t nStart = strStatement.find_first_not_of(' ');
    if (string::npos == nStart)
    {
        return 0;
    }
    size_t nEnd = strStatement.find(' ', nStart);
    m_TableName = strStatement.substr(nStart, (string::npos == nEnd) ? string::npos : nEnd - nStart);
    strTrimRight(m_TableName, ";");
    if (string::npos == nEnd)
    {
        return 1;
    }

    list< pair<string, string> > listValues;
    vParseDbcValueDescriptions(strStatement.substr(nEnd), listValues);

    list< pair<string, string> >::iterator itValue;
    for (itValue = listValues.begin(); itValue != listValues.end(); ++itValue)
    {
        CValueDescriptor valDesc;
        valDesc.m_value.i64Value = nParseDbcInt64(itValue->first.c_str());
        valDesc.m_acDescriptor = itValue->second;
        replace(valDesc.m_acDescriptor.begin(), valDesc.m_acDescriptor.end(), '"', '\'');
        m_values.push_back(valDesc);
    }

    return 1;
}

/**
 * \brief     writes the value tebles in the given list to the output file
 * \param[in] fileOutput Pointer to the Output file
 * \param[in] vTab List of Value tables
 *
 * Writes the value tebles in the given list to the output file.
 */
void CValueTable::writeValueTabToFile(fstream& fileOutput, list<CValueTable>& vTab)
{
    list<CValueTable>::iterator tab;

    for (tab=vTab.begin(); tab!=vTab.end(); ++tab)
    {
        fileOutput << T_ST_TAB " ";
        fileOutput << tab->m_TableName.c_str();
        fileOutput << endl;
        CValueDescriptor desc;
        desc.writeValueDescToFile(fileOutput, CSignal::SIG_TYPE_INT64, tab->m_values);
        fileOutput << T_END_TAB << endl;
    }
}
