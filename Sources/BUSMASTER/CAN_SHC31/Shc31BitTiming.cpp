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
 * \file           Shc31BitTiming.cpp
 * \brief          Pure, host-testable FDCAN bit-timing calculator for the DSD SH-C31x (gs_usb) driver

 *
 * \section DESCRIPTION
 * Implementation of the FDCAN bit-timing calculation for the dsdtech
 * SH-C31G adapters (FDCAN kernel clock 170 MHz).
 * Emits the 5-field gs_device_bittiming wire structure. See
 * Shc31BitTiming.h for the full contract and constraint sets.
 *
 */

#include "Shc31BitTiming.h"

#include <stddef.h>

/** Rounding half constant used when deriving the sample-point segment from
    the target sample point in basis points: x = (SP_bp * Tq + 5000) / 10000 */
#define SHC31_TIMING_BP_ROUND_HALF                      5000U
#define SHC31_TIMING_BP_DENOMINATOR                     10000U

/** Permille scale factor for the bitrate error comparison */
#define SHC31_TIMING_PERMILLE                           1000U

/** u64 cast shorthand for the cross-multiplied u64 comparisons */
#define SHC31_TIMING_U64(x)                             ((uint64_t)(x))

/**
 * Per-phase constraint set derived from the gs_usb BT_CONST_EXT answer
 * (nominal and data phases differ).
 */
typedef struct SHC31_TIMING_CONSTRAINTS_STRUCT
{
    uint32_t unSeg1Min;                          /** Minimum tseg1 in Tq */
    uint32_t unSeg1Max;                          /** Maximum tseg1 in Tq */
    uint32_t unSeg2Min;                          /** Minimum tseg2 in Tq */
    uint32_t unSeg2Max;                          /** Maximum tseg2 in Tq */
    uint32_t unPrescalerMax;                     /** Maximum brp (minimum is 1, increment 1) */
    uint32_t unPreferredTq;                      /** Preferred total Tq for ranking ties */
} Shc31ConstraintsStruct;

/**
 * Applies the requested SJW policy and fills the output structure from the
 * winning candidate.
 *
 * \param unRequestedSjw  Requested SJW in Tq, 0 selects the automatic policy
 *                        min(tseg2, SHC31_TIMING_SJW_AUTO_MAX)
 * \param unPrescaler     Winning brp
 * \param unSeg1          Winning tseg1 (total)
 * \param unSeg2          Winning tseg2
 * \param psResult        Output structure to fill
 *
 * \return None
 */
void Shc31TimingApplyResult(uint32_t unRequestedSjw, uint32_t unPrescaler, uint32_t unSeg1, uint32_t unSeg2, Shc31TimingStruct* psResult);

/**
 * Derives the segments for one (brp, totalTq) candidate from the target
 * sample point, clamping into the constraint ranges. A candidate is valid
 * when both segments end up inside the ranges.
 *
 * \param psConstraints   Constraint set of the phase
 * \param unTotalTq       Total Tq of the candidate
 * \param unTargetSpBp    Target sample point in basis points
 * \param punSeg1         Output: total tseg1
 * \param punSeg2         Output: tseg2
 *
 * \return true when the candidate is valid within the ranges
 */
bool Shc31TimingDeriveSegments(const Shc31ConstraintsStruct* psConstraints, uint32_t unTotalTq, uint32_t unTargetSpBp, uint32_t* punSeg1, uint32_t* punSeg2);

/**
 * Fills the constraint set for a phase.
 *
 * \param ePhase          Phase selector
 * \param psConstraints   Output constraint set
 *
 * \return true when ePhase is valid
 */
bool Shc31TimingGetConstraints(Shc31PhaseEnum ePhase, Shc31ConstraintsStruct* psConstraints)
{
    bool bResult = false;

    if (ePhase == SHC31_PHASE_NOMINAL)
    {
        psConstraints->unSeg1Min = SHC31_TIMING_NOM_SEG1_MIN;
        psConstraints->unSeg1Max = SHC31_TIMING_NOM_SEG1_MAX;
        psConstraints->unSeg2Min = SHC31_TIMING_NOM_SEG2_MIN;
        psConstraints->unSeg2Max = SHC31_TIMING_NOM_SEG2_MAX;
        psConstraints->unPrescalerMax = SHC31_TIMING_NOM_PRESCALER_MAX;
        psConstraints->unPreferredTq = SHC31_TIMING_TQ_PREFERRED_NOMINAL;
        bResult = true;
    }
    else if (ePhase == SHC31_PHASE_DATA)
    {
        psConstraints->unSeg1Min = SHC31_TIMING_DATA_SEG1_MIN;
        psConstraints->unSeg1Max = SHC31_TIMING_DATA_SEG1_MAX;
        psConstraints->unSeg2Min = SHC31_TIMING_DATA_SEG2_MIN;
        psConstraints->unSeg2Max = SHC31_TIMING_DATA_SEG2_MAX;
        psConstraints->unPrescalerMax = SHC31_TIMING_DATA_PRESCALER_MAX;
        psConstraints->unPreferredTq = SHC31_TIMING_TQ_PREFERRED_DATA;
        bResult = true;
    }
    else
    {
        bResult = false;
    }

    return bResult;
}

/**
 * Applies the requested SJW policy and fills the output structure from the
 * winning candidate.
 *
 * \param unRequestedSjw  Requested SJW in Tq, 0 selects the automatic policy
 * \param unPrescaler     Winning brp
 * \param unSeg1          Winning tseg1 (total)
 * \param unSeg2          Winning tseg2
 * \param psResult        Output structure to fill
 *
 * \return None
 */
void Shc31TimingApplyResult(uint32_t unRequestedSjw, uint32_t unPrescaler, uint32_t unSeg1, uint32_t unSeg2, Shc31TimingStruct* psResult)
{
    uint32_t unSjw = unSeg2;

    if (0U == unRequestedSjw)
    {
        if (unSeg2 > SHC31_TIMING_SJW_AUTO_MAX)
        {
            unSjw = SHC31_TIMING_SJW_AUTO_MAX;
        }
    }
    else if (unRequestedSjw < unSeg2)
    {
        unSjw = unRequestedSjw;
    }
    else
    {
        /** SJW is clamped to tseg2: FDCAN requires SJW <= tseg2 */
        unSjw = unSeg2;
    }

    psResult->sWire.PropSeg = unSeg1 / 2U;
    psResult->sWire.PhaseSeg1 = unSeg1 - (unSeg1 / 2U);
    psResult->sWire.PhaseSeg2 = unSeg2;
    psResult->sWire.Sjw = unSjw;
    psResult->sWire.Brp = unPrescaler;
    psResult->TimeSeg1 = unSeg1;
    psResult->TimeSeg2 = unSeg2;
    psResult->TotalTq = 1U + unSeg1 + unSeg2;
    psResult->AchievedBitrate = (uint32_t)(SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) / (SHC31_TIMING_U64(unPrescaler) * SHC31_TIMING_U64(psResult->TotalTq)));
    psResult->SamplePointBp = (uint32_t)((SHC31_TIMING_U64(SHC31_TIMING_BP_DENOMINATOR) * SHC31_TIMING_U64(1U + unSeg1)) / SHC31_TIMING_U64(psResult->TotalTq));
}

/**
 * Derives the segments for one (brp, totalTq) candidate from the target
 * sample point, clamping into the constraint ranges.
 *
 * \param psConstraints   Constraint set of the phase
 * \param unTotalTq       Total Tq of the candidate
 * \param unTargetSpBp    Target sample point in basis points
 * \param punSeg1         Output: total tseg1
 * \param punSeg2         Output: tseg2
 *
 * \return true when the candidate is valid within the ranges
 */
bool Shc31TimingDeriveSegments(const Shc31ConstraintsStruct* psConstraints, uint32_t unTotalTq, uint32_t unTargetSpBp, uint32_t* punSeg1, uint32_t* punSeg2)
{
    bool bValid = false;
    uint64_t ullSyncPlusSeg1 = ((SHC31_TIMING_U64(unTargetSpBp) * SHC31_TIMING_U64(unTotalTq)) + SHC31_TIMING_U64(SHC31_TIMING_BP_ROUND_HALF)) / SHC31_TIMING_U64(SHC31_TIMING_BP_DENOMINATOR);
    uint32_t unSeg1 = 0U;
    uint32_t unSeg2 = 0U;

    if (ullSyncPlusSeg1 < 1U)
    {
        ullSyncPlusSeg1 = 1U;
    }
    else
    {
        /** No lower clamp needed */
    }

    unSeg1 = (uint32_t)ullSyncPlusSeg1 - 1U;
    unSeg2 = unTotalTq - 1U - unSeg1;

    /** Clamp tseg2 into its range, then recompute tseg1 from the fixed
        total: 1 + tseg1 + tseg2 = unTotalTq */
    if (unSeg2 < psConstraints->unSeg2Min)
    {
        unSeg2 = psConstraints->unSeg2Min;
        unSeg1 = unTotalTq - 1U - unSeg2;
    }
    else if (unSeg2 > psConstraints->unSeg2Max)
    {
        unSeg2 = psConstraints->unSeg2Max;
        unSeg1 = unTotalTq - 1U - unSeg2;
    }
    else
    {
        /** tseg2 already inside the range */
    }

    /** Clamp tseg1 into its range; the tseg2 recompute may violate tseg2,
        which makes the candidate invalid */
    if (unSeg1 < psConstraints->unSeg1Min)
    {
        unSeg1 = psConstraints->unSeg1Min;
        unSeg2 = unTotalTq - 1U - unSeg1;
    }
    else if (unSeg1 > psConstraints->unSeg1Max)
    {
        unSeg1 = psConstraints->unSeg1Max;
        unSeg2 = unTotalTq - 1U - unSeg1;
    }
    else
    {
        /** tseg1 already inside the range */
    }

    if ((unSeg1 >= psConstraints->unSeg1Min) && (unSeg1 <= psConstraints->unSeg1Max) &&
            (unSeg2 >= psConstraints->unSeg2Min) && (unSeg2 <= psConstraints->unSeg2Max))
    {
        *punSeg1 = unSeg1;
        *punSeg2 = unSeg2;
        bValid = true;
    }
    else
    {
        bValid = false;
    }

    return bValid;
}

/**
 * As Shc31TimingCalculate, but with an explicitly requested SJW. See the
 * header for the full contract and the candidate selection rule.
 *
 * \param unTargetBitrate       Target bitrate in bit/s, must be > 0
 * \param unTargetSamplePointBp Target sample point in basis points
 * \param unRequestedSjw        Requested SJW in Tq (0 = automatic)
 * \param ePhase                SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 * \param psResult              Output structure, written only on success
 *
 * \return SHC31_TIMING_OK, SHC31_TIMING_OK_NEAREST, SHC31_TIMING_ERR_PARAM
 *         or SHC31_TIMING_ERR_NOT_ACHIEVABLE
 */
Shc31TimingStatusEnum Shc31TimingCalculateSjw(uint32_t unTargetBitrate, uint32_t unTargetSamplePointBp, uint32_t unRequestedSjw, Shc31PhaseEnum ePhase, Shc31TimingStruct* psResult)
{
    Shc31TimingStatusEnum eStatus = SHC31_TIMING_ERR_PARAM;
    Shc31ConstraintsStruct sConstraints;
    uint32_t unPrescaler = 0U;
    uint32_t unBestInTolPrescaler = 0U;
    uint32_t unBestInTolSeg1 = 0U;
    uint32_t unBestInTolSeg2 = 0U;
    uint32_t unBestInTolTotalTq = 0U;
    uint64_t ullBestInTolSpErr = 0U;
    uint64_t ullBestInTolRateDiff = 0U;
    uint64_t ullBestInTolDenTotal = 0U;
    uint32_t unBestInTolTqDist = 0U;
    uint32_t unBestNearPrescaler = 0U;
    uint32_t unBestNearSeg1 = 0U;
    uint32_t unBestNearSeg2 = 0U;
    uint32_t unBestNearTotalTq = 0U;
    uint64_t ullBestNearSpErr = 0U;
    uint64_t ullBestNearRateDiff = 0U;
    uint64_t ullBestNearDenTotal = 0U;
    bool bFoundInTol = false;
    bool bFoundNear = false;
    bool bConstraintsKnown = false;

    if ((psResult == nullptr) || (unTargetBitrate == 0U) ||
            (unTargetSamplePointBp < SHC31_TIMING_SP_BP_MIN) ||
            (unTargetSamplePointBp > SHC31_TIMING_SP_BP_MAX))
    {
        eStatus = SHC31_TIMING_ERR_PARAM;
    }
    else
    {
        bConstraintsKnown = Shc31TimingGetConstraints(ePhase, &sConstraints);

        if (bConstraintsKnown == false)
        {
            eStatus = SHC31_TIMING_ERR_PARAM;
        }
        else
        {
            for (unPrescaler = 1U; unPrescaler <= sConstraints.unPrescalerMax; unPrescaler++)
            {
                uint64_t ullDen = SHC31_TIMING_U64(unPrescaler) * SHC31_TIMING_U64(unTargetBitrate);
                uint64_t ullTotalTq = (SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) + (ullDen / 2U)) / ullDen;
                uint32_t unSeg1 = 0U;
                uint32_t unSeg2 = 0U;
                uint32_t unTotalTq = 0U;

                /** Clamp the rounded total into the expressible range; the
                    nearest pool is evaluated from the clamped value so an
                    out-of-range target still yields the nearest-achievable
                    candidate */
                if (ullTotalTq < SHC31_TIMING_U64(SHC31_TIMING_TOTAL_TQ_MIN))
                {
                    ullTotalTq = SHC31_TIMING_U64(SHC31_TIMING_TOTAL_TQ_MIN);
                }

                {
                    uint64_t ullMaxTotalTq = SHC31_TIMING_U64(1U + sConstraints.unSeg1Max + sConstraints.unSeg2Max);

                    if (ullTotalTq > ullMaxTotalTq)
                    {
                        ullTotalTq = ullMaxTotalTq;
                    }
                }

                unTotalTq = (uint32_t)ullTotalTq;

                if (Shc31TimingDeriveSegments(&sConstraints, unTotalTq, unTargetSamplePointBp, &unSeg1, &unSeg2) == false)
                {
                    continue;
                }

                {
                    uint64_t ullDenTotal = ullDen * SHC31_TIMING_U64(unTotalTq);
                    uint64_t ullRateDiff = 0U;
                    uint64_t ullErrPermille = 0U;
                    uint64_t ullSpErr = 0U;
                    uint32_t unTqDist = 0U;
                    bool bWithinTolerance = false;

                    if (SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) >= ullDenTotal)
                    {
                        ullRateDiff = SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) - ullDenTotal;
                    }
                    else
                    {
                        ullRateDiff = ullDenTotal - SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ);
                    }

                    ullErrPermille = (ullRateDiff * SHC31_TIMING_U64(SHC31_TIMING_PERMILLE)) / ullDenTotal;
                    bWithinTolerance = (ullErrPermille <= SHC31_TIMING_U64(SHC31_TIMING_BITRATE_TOL_PERMILLE)) ? true : false;

                    {
                        uint64_t ullSpExact = SHC31_TIMING_U64(SHC31_TIMING_BP_DENOMINATOR) * SHC31_TIMING_U64(1U + unSeg1);
                        uint64_t ullSpTarget = SHC31_TIMING_U64(unTargetSamplePointBp) * SHC31_TIMING_U64(unTotalTq);

                        if (ullSpExact >= ullSpTarget)
                        {
                            ullSpErr = ullSpExact - ullSpTarget;
                        }
                        else
                        {
                            ullSpErr = ullSpTarget - ullSpExact;
                        }
                    }

                    if (ullTotalTq >= SHC31_TIMING_U64(sConstraints.unPreferredTq))
                    {
                        unTqDist = (uint32_t)(ullTotalTq - SHC31_TIMING_U64(sConstraints.unPreferredTq));
                    }
                    else
                    {
                        unTqDist = (uint32_t)(SHC31_TIMING_U64(sConstraints.unPreferredTq) - ullTotalTq);
                    }

                    if (bWithinTolerance == true)
                    {
                        bool bBetter = false;

                        if (bFoundInTol == false)
                        {
                            bBetter = true;
                        }
                        else if ((ullSpErr * SHC31_TIMING_U64(unBestInTolTotalTq)) < (ullBestInTolSpErr * SHC31_TIMING_U64(unTotalTq)))
                        {
                            /** Relative sample-point error is smaller */
                            bBetter = true;
                        }
                        else if ((ullSpErr * SHC31_TIMING_U64(unBestInTolTotalTq)) == (ullBestInTolSpErr * SHC31_TIMING_U64(unTotalTq)))
                        {
                            if ((ullRateDiff * ullBestInTolDenTotal) < (ullBestInTolRateDiff * ullDenTotal))
                            {
                                /** Relative bitrate error is smaller */
                                bBetter = true;
                            }
                            else if ((ullRateDiff * ullBestInTolDenTotal) == (ullBestInTolRateDiff * ullDenTotal))
                            {
                                if (unTqDist < unBestInTolTqDist)
                                {
                                    bBetter = true;
                                }
                                else if ((unTqDist == unBestInTolTqDist) && (unTotalTq > unBestInTolTotalTq))
                                {
                                    /** Prefer the larger Tq count (finer resolution) */
                                    bBetter = true;
                                }
                                else if ((unTqDist == unBestInTolTqDist) && (unTotalTq == unBestInTolTotalTq) && (unPrescaler < unBestInTolPrescaler))
                                {
                                    bBetter = true;
                                }
                                else
                                {
                                    bBetter = false;
                                }
                            }
                            else
                            {
                                bBetter = false;
                            }
                        }
                        else
                        {
                            bBetter = false;
                        }

                        if (bBetter == true)
                        {
                            bFoundInTol = true;
                            unBestInTolPrescaler = unPrescaler;
                            unBestInTolSeg1 = unSeg1;
                            unBestInTolSeg2 = unSeg2;
                            unBestInTolTotalTq = unTotalTq;
                            ullBestInTolSpErr = ullSpErr;
                            ullBestInTolRateDiff = ullRateDiff;
                            ullBestInTolDenTotal = ullDenTotal;
                            unBestInTolTqDist = unTqDist;
                        }
                    }
                    else
                    {
                        bool bBetter = false;

                        if (bFoundNear == false)
                        {
                            bBetter = true;
                        }
                        else if ((ullRateDiff * ullBestNearDenTotal) < (ullBestNearRateDiff * ullDenTotal))
                        {
                            /** Nearest mode: relative bitrate error is the
                                primary ranking key */
                            bBetter = true;
                        }
                        else if ((ullRateDiff * ullBestNearDenTotal) == (ullBestNearRateDiff * ullDenTotal))
                        {
                            if ((ullSpErr * SHC31_TIMING_U64(unBestNearTotalTq)) < (ullBestNearSpErr * SHC31_TIMING_U64(unTotalTq)))
                            {
                                bBetter = true;
                            }
                            else if ((ullSpErr * SHC31_TIMING_U64(unBestNearTotalTq)) == (ullBestNearSpErr * SHC31_TIMING_U64(unTotalTq)))
                            {
                                if (unTotalTq > unBestNearTotalTq)
                                {
                                    bBetter = true;
                                }
                                else if ((unTotalTq == unBestNearTotalTq) && (unPrescaler < unBestNearPrescaler))
                                {
                                    bBetter = true;
                                }
                                else
                                {
                                    bBetter = false;
                                }
                            }
                            else
                            {
                                bBetter = false;
                            }
                        }
                        else
                        {
                            bBetter = false;
                        }

                        if (bBetter == true)
                        {
                            bFoundNear = true;
                            unBestNearPrescaler = unPrescaler;
                            unBestNearSeg1 = unSeg1;
                            unBestNearSeg2 = unSeg2;
                            unBestNearTotalTq = unTotalTq;
                            ullBestNearSpErr = ullSpErr;
                            ullBestNearRateDiff = ullRateDiff;
                            ullBestNearDenTotal = ullDenTotal;
                        }
                    }
                }
            }

            if (bFoundInTol == true)
            {
                Shc31TimingApplyResult(unRequestedSjw, unBestInTolPrescaler, unBestInTolSeg1, unBestInTolSeg2, psResult);
                eStatus = SHC31_TIMING_OK;
            }
            else if (bFoundNear == true)
            {
                Shc31TimingApplyResult(unRequestedSjw, unBestNearPrescaler, unBestNearSeg1, unBestNearSeg2, psResult);
                eStatus = SHC31_TIMING_OK_NEAREST;
            }
            else
            {
                eStatus = SHC31_TIMING_ERR_NOT_ACHIEVABLE;
            }
        }
    }

    return eStatus;
}

/**
 * Computes nominal or data-phase bit-timing parameters for a target bitrate
 * and target sample point with the automatic SJW policy. See the header for
 * the full contract and the candidate selection rule.
 *
 * \param unTargetBitrate       Target bitrate in bit/s, must be > 0
 * \param unTargetSamplePointBp Target sample point in basis points
 * \param ePhase                SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 * \param psResult              Output structure, written only on success
 *
 * \return See Shc31TimingCalculateSjw
 */
Shc31TimingStatusEnum Shc31TimingCalculate(uint32_t unTargetBitrate, uint32_t unTargetSamplePointBp, Shc31PhaseEnum ePhase, Shc31TimingStruct* psResult)
{
    return Shc31TimingCalculateSjw(unTargetBitrate, unTargetSamplePointBp, 0U, ePhase, psResult);
}

/**
 * Computes the maximum bitrate expressible in a phase at brp = 1 with the
 * minimum segment lengths.
 *
 * \param ePhase  SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 *
 * \return Maximum achievable bitrate in bit/s (rounded down); 0 for an
 *         invalid phase
 */
uint32_t Shc31TimingMaxBitrate(Shc31PhaseEnum ePhase)
{
    uint32_t unBitrate = 0U;
    Shc31ConstraintsStruct sConstraints;

    if (Shc31TimingGetConstraints(ePhase, &sConstraints) == true)
    {
        unBitrate = (uint32_t)(SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) /
                               SHC31_TIMING_U64(1U + sConstraints.unSeg1Min + sConstraints.unSeg2Min));
    }
    else
    {
        unBitrate = 0U;
    }

    return unBitrate;
}

/**
 * Computes the minimum bitrate expressible in a phase at the maximum brp
 * with the maximum segment lengths.
 *
 * \param ePhase  SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 *
 * \return Minimum achievable bitrate in bit/s (rounded down); 0 for an
 *         invalid phase
 */
uint32_t Shc31TimingMinBitrate(Shc31PhaseEnum ePhase)
{
    uint32_t unBitrate = 0U;
    Shc31ConstraintsStruct sConstraints;

    if (Shc31TimingGetConstraints(ePhase, &sConstraints) == true)
    {
        unBitrate = (uint32_t)(SHC31_TIMING_U64(SHC31_TIMING_CLOCK_HZ) /
                               SHC31_TIMING_U64(sConstraints.unPrescalerMax) /
                               SHC31_TIMING_U64(1U + sConstraints.unSeg1Max + sConstraints.unSeg2Max));
    }
    else
    {
        unBitrate = 0U;
    }

    return unBitrate;
}

