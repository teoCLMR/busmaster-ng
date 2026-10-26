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
 * \file      Signal.cpp
 * \brief     Implementation of signal class
 * \authors   Amitesh Bharti, Pemmaiah BD, Mahesh.B.S, Tobias Lorenz
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Implementation of the signal class.
 */

/* C++ includes */
#include <algorithm>
#include <climits>

/* Project includes */
#include "DBFConverter.h"
#include "DBFSignal.h"
#include "DBFTagDefinitions.h"
#include "Utility.h"
#include "Math.h"

using namespace std;

/**
 * \brief Constructor
 *
 * Constructor of CSignal
 */
CSignal::CSignal()
{
    m_acMultiplex = "";
    m_acName = "";
    m_ucLength = 1; // minimum signal length should be 1 bit
    m_ucWhichByte = 1; // ONE based index
    m_ucStartBit = 0; // ZERO based index
    m_ucType = SIG_TYPE_BOOL; // let default be bool type -- match with default length
    m_MaxValue.iValue = 1; // use unsigned int field for bool also
    m_MaxValue.uiValue = 1;
    m_MaxValue.fValue = 1;
    m_MaxValue.dValue = 1;
    m_MaxValue.cValue = "";
    m_MaxValue.i64Value = 1;
    m_MaxValue.ui64Value = 1;
    m_MinValue.iValue = 0;
    m_MinValue.uiValue = 0;
    m_MinValue.fValue = 0;
    m_MinValue.dValue = 0;
    m_MinValue.cValue = "";
    m_MinValue.i64Value = 0;
    m_MinValue.ui64Value = 0;
    m_ucDataFormat = SIG_DF_INTEL;
    m_fOffset = 0.0f;
    m_fScaleFactor = 1.0f;
    m_acUnit = "";
    m_uiError = SIG_EC_NO_ERR;
    m_rxNode = "";
    m_listValueDescriptor.clear();
}


/**
 * \brief Destructor
 *
 * Destructor of CSignal
 */
CSignal::~CSignal()
{
    // clear the embedded value descriptor list before destroying the signal
    m_listValueDescriptor.clear();
}


/**
 * \brief     overloaded operator =
 * \param[in] signal Data to assign
 * \return    Reference to local object
 *
 * Overload assignment operator. We should clear the existing value descriptor
 * list and copy contents of the argument's list to this signal. The rest of the
 * members are copied by value
 */
CSignal& CSignal::operator=(const CSignal& signal)
{
    m_listValueDescriptor.clear(); // clear the list first
    // copy all the data members except the list
    m_acMultiplex = signal.m_acMultiplex;
    m_acName = signal.m_acName;
    m_ucLength = signal.m_ucLength;
    m_ucWhichByte = signal.m_ucWhichByte;
    m_ucStartBit = signal.m_ucStartBit;
    m_ucType = signal.m_ucType;
    m_MaxValue = signal.m_MaxValue;
    m_MinValue = signal.m_MinValue;
    m_ucDataFormat = signal.m_ucDataFormat;
    m_fOffset = signal.m_fOffset;
    m_fScaleFactor = signal.m_fScaleFactor;
    m_acUnit = signal.m_acUnit;
    m_rxNode = signal.m_rxNode;
    m_uiError = signal.m_uiError;
    // now copy the list
    m_listValueDescriptor = signal.m_listValueDescriptor;
    return (*this);
}

/**
* \brief         Replaces first occurance of a character in a char array with a new character
* \param[out]    Char array to be used
* \param[in]     character to be replaced
* \param[in]     new character used for replacement
* \return        S_OK for success, S_FALSE for failure
* \authors       Arunkumar Karri
* \date          03.01.2013 Created
*/
void vReplaceChar(char str[], char chOld, char chNew)
{
    char* ptrOccured = NULL;
    ptrOccured = strchr(str, chOld);
    if ( ptrOccured != 0)
    {
        *ptrOccured = chNew;
    }
}

/**
 * \brief     Extracts the message data from the given Line
 * \param[in] pcLine the given Line
 * \return    Status code
 *
 * Extracts the message data from the given Line and populates
 * the message structure.
 */
int CSignal::Format(char* pcLine)
{
    char* pcToken;
    char *pcTemp,*pcTok;
    // BUSMASTER-NG: the buffer is as long as the line (a fixed buffer overflowed with long receiver lists)
    vector<char> ouTemp(strlen(pcLine) + 2, '\0');
    char* acTemp = &ouTemp[0];
    pcTemp = acTemp;
    // get signal name
    // in older versions of CANoe it can be in any of this format
    // <SIG_NAME :> -- standard signal
    // <SIG_NAME M :> -- mode signal
    // <SIG_NAME mk :> -- mode dependent signal
    pcToken = strtok_s(pcLine, ":", &pcTok); // get upto colon
    if (nullptr == pcToken)
    {
        return 0;
    }

    // copy only signal name because we BUSMASTER does not support modes
    // skip leading spaces first
    while(*pcToken && *pcToken == ' ')
    {
        pcToken++;
    }

    // now get the signal name
    while(*pcToken && *pcToken != ' ')
    {
        *pcTemp++ = *pcToken++; // copy SIG_NAME only, i.e. till first 'space'
    }

    *pcTemp = '\0'; // terminate it
    m_acName = acTemp; // copy the name to the signal's data member
    pcTemp = acTemp; // reset pcTemp to start of buffer
    //leave blank space
    while(*pcToken && *pcToken == ' ')
    {
        pcToken++;
    }

    //Find the signal's multiplexing details
    while(*pcToken && *pcToken != ' ')
    {
        *pcTemp++ = *pcToken++; // copy SIG_NAME only, i.e. till first 'space'
    }

    *pcTemp = '\0'; // terminate it
    m_acMultiplex = acTemp; // copy the name to the signal's data member
    pcTemp = acTemp; // reset pcTemp to start of buffer
    // next token (START_BIT|LENGTH@DATA_FORMAT(+/-))
    pcToken = strtok_s(NULL, " :", &pcTok);
    if ((nullptr == pcToken) || (nullptr == strchr(pcToken, '|')) || (nullptr == strchr(pcToken, '@')))
    {
        return 0;   // not a valid signal definition
    }

    // get start bit
    while(*pcToken && *pcToken != '|')
    {
        *pcTemp++ = *pcToken++;
    }

    *pcTemp='\0';
    pcToken++; // skip '|'
    // store the start byte and start bit information
    int uiStartBit = atoi(acTemp);
    int uiStartBitX = uiStartBit;
    // rajesh: 21-03-2003: begin:
    // based on INTEL/MOTOROLA - the start bit is represnted differently by CANoe.
    // With motorola format, bits are stored in reverse order. BUSMASTER
    // does not differentiate between INTEL and MOTOROLA at this stage and START_BYTE
    // and START_BIT are not depending on this. So process this after identifying the
    // format. Original code commented - moved down.
    // rajesh: 21-03-2003: end_1: continued below
    // get signal length
    pcTemp = acTemp;

    while(*pcToken && *pcToken != '@')
    {
        *pcTemp++ = *pcToken++;
    }

    *pcTemp='\0';
    pcToken++; // skip '@'
    m_ucLength = strtoul(acTemp, NULL, 10); // store signal length
    // get DATA_FORMAT (intel or motorola)
    m_ucDataFormat = *pcToken;

    if(SIG_DF_MOTOROLA == m_ucDataFormat)
    {
        //The following is done because if signal type is of MOTOROLA
        //THe MSB is stored and will be in Big Endian format.
        //Get the LSB
        /*In CANoe if we view the bits as a 8x8 Matrix, for Motorala signals
        the numbering will be in the reverse order with in the each row. So to get the
        proper MSB we need to again swap the bit position with in the row*/
        //i.e here we need to get the left half of the bits to the right and right
        //to the left with in the same row
        int ntemp;

        if(uiStartBit == 0)
        {
            ntemp = 0;
        }
        else
        {
            ntemp = uiStartBit % 4;    //get the bit position with its the row.
        }

        //swap left bits to the right and right bits to the left.
        if(uiStartBit % 8 <= 3)
        {
            uiStartBit = uiStartBit + 7 - ntemp * 2;
        }
        else
        {
            uiStartBit = uiStartBit - 1 - ntemp * 2;
        }

        uiStartBit = uiStartBit + m_ucLength-1;
        //Invert the bit position (Converting to little endian format)
        uiStartBitX = (CDBFConverter::ucMsg_DLC*8)-1 - uiStartBit;
    }

    m_ucWhichByte = uiStartBit / 8 + 1;
    m_ucStartBit = uiStartBitX % 8;

    // get sign of signal. At this point we know only whether the
    // signal is signed or unsigned. Whether it is float or double
    // will be known later only when we process "SIG_VALTYPE_"
    if(m_ucLength > 32)
    {
        m_ucType = (*(++pcToken) == '-') ? SIG_TYPE_INT64 : SIG_TYPE_UINT64;
    }
    else
    {
        m_ucType = (*(++pcToken) == '-') ? SIG_TYPE_INT : SIG_TYPE_UINT;
    }

    // BUSMASTER-NG: (FACTOR,OFFSET) [MIN|MAX] are read from the rest of the line, so spaces inside the brackets
    // are allowed, and the numbers are read with '.' as decimal symbol whatever the language of Windows.
    // The old code used the Windows decimal symbol: on an Italian or German PC 0.5 became 0 (issues 498, 570).
    char* pcOpen = strchr(pcTok, '(');
    char* pcClose = (nullptr != pcOpen) ? strchr(pcOpen, ')') : nullptr;
    char* pcSqOpen = (nullptr != pcClose) ? strchr(pcClose, '[') : nullptr;
    char* pcSqClose = (nullptr != pcSqOpen) ? strchr(pcSqOpen, ']') : nullptr;
    char* pcComma = (nullptr != pcClose) ? strchr(pcOpen, ',') : nullptr;
    char* pcBar = (nullptr != pcSqClose) ? strchr(pcSqOpen, '|') : nullptr;
    if ((nullptr == pcSqClose) || (nullptr == pcComma) || (pcComma > pcClose) || (nullptr == pcBar) || (pcBar > pcSqClose))
    {
        return 0;   // not a valid signal definition
    }
    *pcComma = '\0';
    *pcClose = '\0';
    *pcBar = '\0';
    *pcSqClose = '\0';
    m_fScaleFactor = dParseDbcDouble(pcOpen + 1);
    m_fOffset = dParseDbcDouble(pcComma + 1);
    m_MinValue.dValue = dParseDbcDouble(pcSqOpen + 1); // store MIN
    m_MaxValue.dValue = dParseDbcDouble(pcBar + 1);    // store MAX value
    // next token -- "UNIT", ""
    pcTemp = acTemp;
    pcToken = pcSqClose + 1;

    // go to '"'
    while(*pcToken && *pcToken != '\"')
    {
        pcToken++;
    }

    if (*pcToken)
    {
        pcToken++; // skip '"'
    }

    // copy everything, but not including the last <">
    while(*pcToken && *pcToken != '\"')
    {
        *pcTemp++ = *pcToken++;
    }

    if (*pcToken)
    {
        pcToken++; // skip '"'
    }
    *pcTemp='\0';
    m_acUnit = acTemp; // copy UNIT to corresponding data member.
    replace(m_acUnit.begin(), m_acUnit.end(), ',', '_'); //" not supported

    // skip ' '
    while(*pcToken && *pcToken == ' ')
    {
        pcToken++;
    }

    pcTemp = acTemp;

    while(*pcToken)
    {
        if ((*pcToken != '\r') && (*pcToken != ' '))
        {
            *pcTemp++ = *pcToken++;
        }
        else
        {
            pcToken++;
        }
    }

    *pcTemp='\0';

    // copy rx nodes
    if(strcmp(acTemp, "Vector__XXX") != 0)
    {
        m_rxNode = acTemp;
    }
    else
    {
        m_rxNode = "";
    }

    return 1;
}


/**
 * \brief Extracts the value descriptor data from the given Line
 * \param[in] pcLine The given Line
 * \param[in] fileInput Input file
 * \return    Status Code
 *
 * Extracts the value descriptor data from the given Line and adds
 * to the signal.
 */
int CSignal::AddValueDescriptors(char* pcLine, fstream& fileInput)
{
    // BUSMASTER-NG: the statement may continue on the next lines. The old code joined them into a 4-byte copy
    // (sizeof of a pointer) and used fixed buffers, so long or multi-line VAL_ lists were cut or crashed.
    string strStatement = pcLine;
    vReadDbcStatement(fileInput, strStatement);

    list< pair<string, string> > listValues;
    vParseDbcValueDescriptions(strStatement, listValues);

    list< pair<string, string> >::iterator itValue;
    for (itValue = listValues.begin(); itValue != listValues.end(); ++itValue)
    {
        CValueDescriptor valDesc;
        // keep every representation; Validate() picks the one that fits the signal type
        valDesc.m_value.dValue = dParseDbcDouble(itValue->first.c_str());
        valDesc.m_value.i64Value = nParseDbcInt64(itValue->first.c_str());
        valDesc.m_value.ui64Value = unParseDbcUInt64(itValue->first.c_str());
        valDesc.m_acDescriptor = itValue->second;
        // the DBF stores the description between quotes
        replace(valDesc.m_acDescriptor.begin(), valDesc.m_acDescriptor.end(), '"', '\'');
        m_listValueDescriptor.push_back(valDesc);
    }

    return 1;
}


/**
 * \brief     Validate for conformance to BUSMASTER DB format
 * \param[in] ucFormat data format
 * \return    SIG_EC_NO_ERR, SIG_EC_DATA_FORMAT_ERR, SIG_EC_LENGTH_ERR, SIG_EC_STARTBIT_ERR, SIG_EC_TYPE_ERR
 *
 * Convert single bit INT and UINT to BOOL.
 * If MAX_value == MIN_value update MAX_value to MAX_default
 * else if MAX_value == MIN_value == MAX_default, update MIN_value to MIN_default
 * signals with length more than 32 bits shall be discarded.
 *
 * The data format of previous signal is passed as a parameter.
 * For the first one it will be zero, for then on either INTEL or MOTOROLA.
 * If this signals format is not matching with previous signal then this signal to be discarded.
 */
unsigned int CSignal::Validate(unsigned char /* ucFormat */)
{
    // errors eliminated now do necessary conversions
    if(m_ucLength == 1) // single bit - change type to bool
    {
        m_ucType = SIG_TYPE_BOOL;
    }

    m_uiError = SIG_EC_NO_ERR;

    // BUSMASTER-NG: the DBF stores MIN and MAX as raw values. They are computed in double precision and limited to
    // what the signal length can hold. The old code converted through 32-bit casts and floats, so big values
    // (for example an offset of -2105540607 or a maximum of 4211081215) became garbage, and 64-bit unsigned
    // signals were limited to half of their range. [0|0] is the usual way to say "no range": it gets the full
    // range without a warning.
    bool bNoRange = ((m_MinValue.dValue == 0.0) && (m_MaxValue.dValue == 0.0));
    double dRawMin = 0.0;
    double dRawMax = 0.0;

    if (m_fScaleFactor == 0.0)
    {
        m_fScaleFactor = 1.0;
        bNoRange = true;
        m_uiError = SIG_EC_OVERFLOW;
    }
    else
    {
        dRawMin = (m_MinValue.dValue - m_fOffset) / m_fScaleFactor;
        dRawMax = (m_MaxValue.dValue - m_fOffset) / m_fScaleFactor;
        if (dRawMin > dRawMax)  // negative factor
        {
            double dTemp = dRawMin;
            dRawMin = dRawMax;
            dRawMax = dTemp;
        }
    }

    unsigned int unBits = (m_ucLength < 1) ? 1 : ((m_ucLength > 64) ? 64 : m_ucLength);

    switch(m_ucType)
    {
        case SIG_TYPE_BOOL:
        case SIG_TYPE_UINT:
        case SIG_TYPE_UINT64:
        {
            unsigned long long int unLimit = (unBits >= 64) ? ULLONG_MAX : ((1ULL << unBits) - 1);
            unsigned long long int unMin = 0;
            unsigned long long int unMax = unLimit;

            if (!bNoRange)
            {
                double dTolerance = 0.5 + 1e-9 * (double)unLimit;
                if ((dRawMin < -dTolerance) || (dRawMax > (double)unLimit + dTolerance))
                {
                    m_uiError = SIG_EC_OVERFLOW;
                }
                unMin = (dRawMin <= 0.0) ? 0 : ((dRawMin >= (double)unLimit) ? unLimit : (unsigned long long int)floor(dRawMin + 0.5));
                unMax = (dRawMax <= 0.0) ? 0 : ((dRawMax >= (double)unLimit) ? unLimit : (unsigned long long int)floor(dRawMax + 0.5));
                if (unMin == unMax)
                {
                    unMin = 0;
                    unMax = unLimit;
                    m_uiError = SIG_EC_OVERFLOW;
                }
            }

            m_MinValue.ui64Value = unMin;
            m_MaxValue.ui64Value = unMax;
            m_MinValue.uiValue = (unsigned int)unMin;
            m_MaxValue.uiValue = (unsigned int)unMax;
        }
        break;

        case SIG_TYPE_INT:
        case SIG_TYPE_INT64:
        {
            long long int nLimit = (unBits >= 64) ? LLONG_MAX : ((1LL << (unBits - 1)) - 1);
            long long int nLowest = -nLimit - 1;
            long long int nMin = nLowest;
            long long int nMax = nLimit;

            if (!bNoRange)
            {
                double dTolerance = 0.5 + 1e-9 * (double)nLimit;
                if ((dRawMin < (double)nLowest - dTolerance) || (dRawMax > (double)nLimit + dTolerance))
                {
                    m_uiError = SIG_EC_OVERFLOW;
                }
                nMin = (dRawMin <= (double)nLowest) ? nLowest : ((dRawMin >= (double)nLimit) ? nLimit : (long long int)floor(dRawMin + 0.5));
                nMax = (dRawMax <= (double)nLowest) ? nLowest : ((dRawMax >= (double)nLimit) ? nLimit : (long long int)floor(dRawMax + 0.5));
                if (nMin == nMax)
                {
                    nMin = nLowest;
                    nMax = nLimit;
                    m_uiError = SIG_EC_OVERFLOW;
                }
            }

            m_MinValue.i64Value = nMin;
            m_MaxValue.i64Value = nMax;
            m_MinValue.iValue = (int)nMin;
            m_MaxValue.iValue = (int)nMax;
        }
        break;

        // float and double: MIN and MAX stay physical values
        case SIG_TYPE_FLOAT:
            m_MaxValue.fValue = (float)m_MaxValue.dValue;
            m_MinValue.fValue = (float)m_MinValue.dValue;
            break;

        case SIG_TYPE_DOUBLE:
            break;

        default:
            break;
    }

    // correct value descriptors according to type of signal (the value is kept in every representation)
    list<CValueDescriptor>::iterator rValDesc;

    for (rValDesc=m_listValueDescriptor.begin(); rValDesc!=m_listValueDescriptor.end(); ++rValDesc)
    {
        switch(m_ucType)
        {
            case SIG_TYPE_BOOL:
            case SIG_TYPE_UINT:
                rValDesc->m_value.uiValue = (unsigned int)rValDesc->m_value.ui64Value;
                break;

            case SIG_TYPE_INT:
                rValDesc->m_value.iValue = (int)rValDesc->m_value.i64Value;
                break;

            case SIG_TYPE_FLOAT:
                rValDesc->m_value.fValue = (float)rValDesc->m_value.dValue;
                break;

            default:
                break;
        }
    }

    return (m_uiError);
}

/**
 * \brief      Get error string
 * \param[out] str The error string
 *
 * This returns an error string in accordance with m_uiError.
 */
void CSignal::GetErrorString(string& str)
{
    switch(m_uiError)
    {
        case SIG_EC_NO_ERR:
            str = _("No error");
            break;

        case SIG_EC_DATA_FORMAT_ERR:
            str = _("Data format mismatch");
            break;

        case SIG_EC_LENGTH_ERR:
            str = _("Invalid signal length");
            break;

        case SIG_EC_STARTBIT_ERR:
            str = _("Invalid start bit");
            break;

        case SIG_EC_TYPE_ERR:
            str = _("Invalid signal type");
            break;

        case SIG_EC_OVERFLOW:
            str = _("Min or max value outside the range of the signal");
            break;

        default:
            str = _("Unknown");
            break;
    }
}

/**
 * \brief      Get error action
 * \param[out] str The error action string
 *
 * This returns a string to describe the action taken in accordance with m_uiError.
 */
void CSignal::GetErrorAction(string& str)
{
    switch(m_uiError)
    {
        case SIG_EC_OVERFLOW:
            str = _("Limited to the range of the signal");
            break;

        default:
            str = _("Signal Discarded");
            break;
    }
}

/**
 * \brief     writes the signals in the given list to the output file
 * \param[in] fileOutput Pointer to the Output file
 * \param[in] m_listSignals List of Signals
 * \param[in] m_ucLength Message length
 * \param[in] m_cDataFormat If 1 dataformat Intel, 0- Motorola
 * \param[in] writeErr If true write error signals also else write onlt correct signals
 * \return    Status code
 *
 * Writes the signals in the given list to the output file.
 */
bool CSignal::WriteSignaltofile(fstream& fileOutput, list<CSignal>& m_listSignals, int m_ucLength, int m_cDataFormat, bool writeErr)
{
    bool bResult = true;
    list<CSignal>::iterator sig;

    for (sig=m_listSignals.begin(); sig!=m_listSignals.end(); ++sig)
    {
        // SIG_NAME,SIG_LENGTH,WHICH_BYTE_IN_MSG,START_BIT,SIG_TYPE,MAX_VAL,MIN_VAL,SIG_DATA_FORMAT,SIG_OFFSET,SIG_FACTOR,SIG_UNIT
        // write signal only if it is valid
        if((sig->m_uiError == CSignal::SIG_EC_NO_ERR) || (sig->m_uiError == CSignal::SIG_EC_OVERFLOW) || writeErr)
        {
            // For signal having motoroal format, the message length could be less
            // then eight byte. so in that case the whichByte needs to be shifted
            // accordingly.
            fileOutput << T_SIG << " " << sig->m_acName.c_str();
            fileOutput << "," << dec << sig->m_ucLength;
            fileOutput << "," << dec << sig->m_ucWhichByte;
            fileOutput << "," << dec << sig->m_ucStartBit;

            switch(sig->m_ucType)
            {
                case CSignal::SIG_TYPE_BOOL:
                case CSignal::SIG_TYPE_UINT:
                    fileOutput << "," << sig->m_ucType;
                    fileOutput << "," << dec << sig->m_MaxValue.uiValue;
                    fileOutput << "," << dec << sig->m_MinValue.uiValue;
                    break;

                case CSignal::SIG_TYPE_INT:
                    fileOutput << "," << sig->m_ucType;
                    fileOutput << "," << dec << sig->m_MaxValue.iValue;
                    fileOutput << "," << dec << sig->m_MinValue.iValue;
                    break;

                case CSignal::SIG_TYPE_FLOAT:
                    fileOutput << "," << sig->m_ucType;
                    fileOutput << "," << strFormatDbfDouble(sig->m_MaxValue.fValue);
                    fileOutput << "," << strFormatDbfDouble(sig->m_MinValue.fValue);
                    break;

                case CSignal::SIG_TYPE_DOUBLE:
                    fileOutput << "," << sig->m_ucType;
                    fileOutput << "," << strFormatDbfDouble(sig->m_MaxValue.dValue);
                    fileOutput << "," << strFormatDbfDouble(sig->m_MinValue.dValue);
                    break;

                case CSignal::SIG_TYPE_INT64:
                    fileOutput << ",I";
                    fileOutput << "," << dec << sig->m_MaxValue.i64Value;
                    fileOutput << "," << dec << sig->m_MinValue.i64Value;
                    break;

                case CSignal::SIG_TYPE_UINT64:
                    // BUSMASTER-NG: written as signed 64-bit, like the database editor does: the database reader
                    // refuses the whole file when a value is above 9223372036854775807
                    fileOutput << ",U";
                    fileOutput << "," << dec << (long long)sig->m_MaxValue.ui64Value;
                    fileOutput << "," << dec << (long long)sig->m_MinValue.ui64Value;
                    break;

                default:
                    break;
            }

            fileOutput << "," << sig->m_ucDataFormat;
            // BUSMASTER-NG: full precision with '.', the stream default (6 digits) wrote -2105540607 as -2.10554e+09
            fileOutput << "," << strFormatDbfDouble(sig->m_fOffset);
            fileOutput << "," << strFormatDbfDouble(sig->m_fScaleFactor);
            fileOutput << "," << sig->m_acUnit.c_str();
            fileOutput << "," << sig->m_acMultiplex.c_str();
            fileOutput << "," << sig->m_rxNode.c_str() << endl;
            CValueDescriptor val;
            val.writeValueDescToFile(fileOutput, sig->m_ucType, sig->m_listValueDescriptor);

            if(sig->m_uiError == CSignal::SIG_EC_OVERFLOW)
            {
                bResult = false;
            }
        }
        else
        {
            bResult = false;
        }
    }

    return bResult;
}
