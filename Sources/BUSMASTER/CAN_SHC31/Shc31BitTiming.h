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
 * \file           Shc31BitTiming.h
 * \brief          Pure, host-testable FDCAN bit-timing calculator for the DSD SH-C31x (gs_usb) driver
 *
 * \section DESCRIPTION
 * Computes FDCAN nominal and data-phase bit-timing parameters for the
 * dsdtech SH-C31G adapters (STM32G431, FDCAN kernel clock 170 000 000 Hz).
 * The output is the 5-field gs_device_bittiming
 * structure {prop_seg, phase_seg1, phase_seg2, sjw, brp} as written to the
 * device via the gs_usb vendor requests BITTIMING (1) and DATA_BITTIMING
 * (10); the firmware consumes tseg1 = prop_seg + phase_seg1
 * (firmware src/usbd_gs_can.c, GS_USB_BREQ_BITTIMING handler).
 *
 * Constraint sets (verified verbatim from the DSD v1.5 firmware patch,
 * USBD_GS_CAN_btconst / USBD_GS_CAN_btconst_extended):
 *   nominal: tseg1 2..256, tseg2 2..128, sjw 1..128, brp 1..512
 *   data:    dtseg1 1..32, dtseg2 1..16, dsjw 1..16, dbrp 1..32
 *
 * Rates that cannot be reached exactly at 170 MHz (e.g. 800k nominal,
 * 4M/8M data) return the nearest-achievable candidate with a distinct
 * status so the caller can report the documented delta.
 *
 * All functions are pure (no I/O, no hardware access, no global mutable
 * state) so the module is unit-testable on a host PC.
 */

#ifndef __INCShc31BitTiming
#define __INCShc31BitTiming

#include <stdint.h>

/** FDCAN kernel clock in Hz. The SH-C31x runs the FDCAN
    kernel clock at 170 MHz (CONFIG_CLK170_SH_C31A: PLLN 85, VCO 340 MHz,
    PLLR /2; verified from the DSD v1.5 patch). TIM2 divides it by 170 for the
    exactly-1 MHz hardware timestamp tick. */
#define SHC31_TIMING_CLOCK_HZ                           170000000U

/** Minimum total time quanta per bit: sync segment (1) + minimum tseg1 (1)
    + minimum tseg2 (1). Used as the lower clamp when deriving the nearest
    total-Tq candidate. */
#define SHC31_TIMING_TOTAL_TQ_MIN                       3U

/** Preferred total time quanta per bit for the nominal (arbitration) phase.
    Heritage of the fork's 80 Tq base timing table; used only as a ranking
    preference between equally accurate candidates (unobservable). */
#define SHC31_TIMING_TQ_PREFERRED_NOMINAL               80U

/** Preferred total time quanta per bit for the data phase. Heritage of the
    fork's 40 Tq base timing table. */
#define SHC31_TIMING_TQ_PREFERRED_DATA                  40U

/** Default automatic SJW cap in Tq. Matches the fork's default SJW of 8;
    the applied SJW is min(tseg2, this value), well inside both the nominal
    (128) and data (16) sjw maxima. */
#define SHC31_TIMING_SJW_AUTO_MAX                       8U

/** Maximum accepted relative bitrate error in permille (10 permille = 1
    percent) for the SHC31_TIMING_OK status. Candidates outside this
    tolerance fall into the nearest-achievable pool (SHC31_TIMING_OK_NEAREST). */
#define SHC31_TIMING_BITRATE_TOL_PERMILLE               10U

/** Minimum accepted target sample point in basis points (0.01 percent
    units): 10 percent */
#define SHC31_TIMING_SP_BP_MIN                          1000U

/** Maximum accepted target sample point in basis points: 99 percent */
#define SHC31_TIMING_SP_BP_MAX                          9900U

/** Nominal phase gs_usb constraint set: tseg1 (= prop_seg + phase_seg1)
    minimum 2 (the shipping firmware's minimum, not the HAL's 1) */
#define SHC31_TIMING_NOM_SEG1_MIN                       2U

/** Nominal phase gs_usb constraint set: tseg1 maximum 256 */
#define SHC31_TIMING_NOM_SEG1_MAX                       256U

/** Nominal phase gs_usb constraint set: tseg2 minimum 2 */
#define SHC31_TIMING_NOM_SEG2_MIN                       2U

/** Nominal phase gs_usb constraint set: tseg2 maximum 128 */
#define SHC31_TIMING_NOM_SEG2_MAX                       128U

/** Nominal phase gs_usb constraint set: sjw maximum 128 */
#define SHC31_TIMING_NOM_SJW_MAX                        128U

/** Nominal phase gs_usb constraint set: brp maximum 512 (brp_min = 1,
    brp_inc = 1) */
#define SHC31_TIMING_NOM_PRESCALER_MAX                  512U

/** Data phase gs_usb constraint set: dtseg1 minimum 1 */
#define SHC31_TIMING_DATA_SEG1_MIN                      1U

/** Data phase gs_usb constraint set: dtseg1 maximum 32 */
#define SHC31_TIMING_DATA_SEG1_MAX                      32U

/** Data phase gs_usb constraint set: dtseg2 minimum 1 */
#define SHC31_TIMING_DATA_SEG2_MIN                      1U

/** Data phase gs_usb constraint set: dtseg2 maximum 16 */
#define SHC31_TIMING_DATA_SEG2_MAX                      16U

/** Data phase gs_usb constraint set: dsjw maximum 16 */
#define SHC31_TIMING_DATA_SJW_MAX                       16U

/** Data phase gs_usb constraint set: dbrp maximum 32 (dbrp_min = 1,
    dbrp_inc = 1) */
#define SHC31_TIMING_DATA_PRESCALER_MAX                 32U

/**
 * Phase selector for the bit-timing calculation.
 */
typedef enum SHC31_TIMING_PHASE_ENUM
{
    SHC31_PHASE_NOMINAL = 0,                     /** Arbitration (nominal) phase */
    SHC31_PHASE_DATA = 1                         /** Data phase (CAN FD) */
} Shc31PhaseEnum;

/**
 * Status returned by all Shc31Timing functions.
 */
typedef enum SHC31_TIMING_STATUS_ENUM
{
    SHC31_TIMING_OK = 0,                         /** Calculation succeeded within the bitrate tolerance */
    SHC31_TIMING_OK_NEAREST,                     /** Target not achievable within tolerance; best achievable candidate returned */
    SHC31_TIMING_ERR_PARAM,                      /** Invalid input parameter */
    SHC31_TIMING_ERR_NOT_ACHIEVABLE              /** No candidate satisfies the segment constraint ranges at all */
} Shc31TimingStatusEnum;

/**
 * The 5-field gs_device_bittiming structure exactly as it is written to the
 * device (little-endian u32 array, 20 bytes) via BITTIMING/DATA_BITTIMING.
 */
typedef struct SHC31_GS_BITTIMING_STRUCT
{
    uint32_t PropSeg;                            /** prop_seg in Tq (first half of tseg1) */
    uint32_t PhaseSeg1;                          /** phase_seg1 in Tq (second half of tseg1) */
    uint32_t PhaseSeg2;                          /** phase_seg2 in Tq (after the sample point) */
    uint32_t Sjw;                                /** Synchronisation jump width in Tq */
    uint32_t Brp;                                /** Bit-rate prescaler */
} Shc31GsBittimingStruct;

/**
 * Result of a bit-timing calculation. Contains the 5-field wire structure
 * plus the derived quantities used by the driver for read-back verification
 * and by the tests for validation.
 */
typedef struct SHC31_TIMING_STRUCT
{
    Shc31GsBittimingStruct sWire;                /** 5-field gs_device_bittiming output */
    uint32_t TimeSeg1;                           /** Total tseg1 = prop_seg + phase_seg1, in Tq */
    uint32_t TimeSeg2;                           /** tseg2 in Tq */
    uint32_t TotalTq;                            /** Total time quanta per bit: 1 + tseg1 + tseg2 */
    uint32_t AchievedBitrate;                    /** Achieved bitrate in bit/s (rounded down) */
    uint32_t SamplePointBp;                      /** Achieved sample point in basis points (0.01 percent units) */
} Shc31TimingStruct;

/**
 * Computes nominal or data-phase bit-timing parameters for a target bitrate
 * and target sample point, with the SJW chosen automatically as
 * min(tseg2, SHC31_TIMING_SJW_AUTO_MAX).
 *
 * Selection rule (documented): among all (brp, tseg1, tseg2) candidates
 * within the constraint ranges and the bitrate tolerance, the candidate
 * with the smallest relative sample-point error wins; ties are broken by
 * relative bitrate error, then by total Tq count closest to the phase's
 * preferred Tq (80 nominal, 40 data), then by the larger total Tq, then by
 * the smaller brp. When no candidate is within the bitrate tolerance, the
 * candidate with the smallest relative bitrate error is returned with
 * status SHC31_TIMING_OK_NEAREST (delta = |AchievedBitrate - target|).
 *
 * \param unTargetBitrate       Target bitrate in bit/s, must be > 0
 * \param unTargetSamplePointBp Target sample point in basis points
 *                              (SHC31_TIMING_SP_BP_MIN..SHC31_TIMING_SP_BP_MAX)
 * \param ePhase                SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 * \param psResult              Output structure, written only on success
 *
 * \return SHC31_TIMING_OK on success, SHC31_TIMING_OK_NEAREST when only a
 *         nearest-achievable candidate exists, SHC31_TIMING_ERR_PARAM for
 *         invalid parameters, SHC31_TIMING_ERR_NOT_ACHIEVABLE when no
 *         candidate satisfies the segment ranges
 */
Shc31TimingStatusEnum Shc31TimingCalculate(uint32_t unTargetBitrate, uint32_t unTargetSamplePointBp, Shc31PhaseEnum ePhase, Shc31TimingStruct* psResult);

/**
 * As Shc31TimingCalculate, but with an explicitly requested SJW. A request
 * of 0 selects the automatic policy. A request above the computed tseg2 is
 * clamped to tseg2 (FDCAN requires SJW <= tseg2); the clamp is silently
 * applied and reflected in the returned structure.
 *
 * \param unTargetBitrate       Target bitrate in bit/s, must be > 0
 * \param unTargetSamplePointBp Target sample point in basis points
 * \param unRequestedSjw        Requested SJW in Tq (0 = automatic)
 * \param ePhase                SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 * \param psResult              Output structure, written only on success
 *
 * \return See Shc31TimingCalculate
 */
Shc31TimingStatusEnum Shc31TimingCalculateSjw(uint32_t unTargetBitrate, uint32_t unTargetSamplePointBp, uint32_t unRequestedSjw, Shc31PhaseEnum ePhase, Shc31TimingStruct* psResult);

/**
 * Computes the maximum bitrate expressible in a phase at brp = 1 with the
 * minimum segment lengths; exposed for tests and driver diagnostics.
 *
 * \param ePhase  SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 *
 * \return Maximum achievable bitrate in bit/s (rounded down)
 */
uint32_t Shc31TimingMaxBitrate(Shc31PhaseEnum ePhase);

/**
 * Computes the minimum bitrate expressible in a phase at the maximum brp
 * with the maximum segment lengths; exposed for tests and driver
 * diagnostics.
 *
 * \param ePhase  SHC31_PHASE_NOMINAL or SHC31_PHASE_DATA
 *
 * \return Minimum achievable bitrate in bit/s (rounded down)
 */
uint32_t Shc31TimingMinBitrate(Shc31PhaseEnum ePhase);

#endif
