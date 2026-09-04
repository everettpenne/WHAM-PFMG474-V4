#ifndef __HRTIM_H__
#define __HRTIM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_hrtim.h"
#include <stdint.h>

/*
 * hrtim.c
 *
 * Ported from the sibling PFM-STM32G474 project's hrtim.c -- 3-phase
 * (Timers A/B/C) complementary PWM generation on HRTIM1, hardcoded to
 * exactly the same 3 channels as that project (see version.h's
 * PWM_NUM_CHANNELS comment for why this isn't generalized to more
 * channels yet, even though this board's pinout wires out 6).
 *
 * NOT ported (out of scope, per project decision, 2026-08-31):
 *   - HRTIM1_EmergencyStop() -- fault-response specific; no fault
 *     system exists in this project yet. Small and self-contained in
 *     the sibling project's hrtim.c; port verbatim when fault handling
 *     is added.
 *   - Anything FEEDBACK-mode related (that lived in pfm.c/feedback.c,
 *     not hrtim.c, and was never in this file to begin with).
 */

extern HRTIM_HandleTypeDef hhrtim1;

/* Core timing -- see the sibling project's hrtim.h for the full
 * derivation (170 MHz HRTIM clock from PLL math, confirmed against the
 * CONFIG command's live SYSCLK readout there; 17 counts of dead time at
 * 170 MHz = 100 ns). Carried over unchanged; this board's clock tree is
 * the same STM32G474 PLL configuration. */
#define HRTIM_TIMER_CLK_HZ      170000000U
#define HRTIM_DEADTIME_NS       100U
#define HRTIM_DEADTIME_COUNTS   17U

/* Safe compare clamping margin */
#define HRTIM_COMPARE_MIN           ((uint16_t)2U)

/* HRTIM timer index helpers for your local header structure */
#define HRTIM_IDX_MASTER            (0U)
#define HRTIM_IDX_A                 (0U)
#define HRTIM_IDX_B                 (1U)
#define HRTIM_IDX_C                 (2U)

void HRTIM1_FullInit(void);

/* Starts HRTIM outputs, selectively enabling only the phases requested.
 * A phase passed as 0 (disabled) never has its outputs enabled -- its
 * HRTIM timer counter still runs (kept synchronized with the Master for
 * coherent-update correctness if the phase is re-enabled later), but no
 * output pin for that phase is driven. */
void HRTIM1_PWM_Start(uint8_t enableU, uint8_t enableV, uint8_t enableW);
void HRTIM1_PWM_Stop(void);

/* Force an immediate transfer of shadow (preload) registers into active
 * registers for the Master timer and all three slave timers (A/B/C).
 *
 * Called at the start of every shot (by PFM_Restart(), before counters
 * begin) to ensure the first PWM period runs with the correct PER/CMP/
 * phase values rather than the stale init defaults from HRTIM1_FullInit().
 * During the shot, the normal repetition-event preload transfer handles
 * subsequent updates without any help; this is a cold-start fix only.
 *
 * Uses the HRTIM_CR2 global software-update bits (MSWU for Master,
 * TxSWU for each slave).  A write to CR2 bypasses the HAL state machine
 * and lock -- safe here because no HRTIM operation is in flight when this
 * is called (counters are stopped, no HAL calls are active). */
void HRTIM1_SoftwareUpdate(void);

/* Coherent register update helpers */
void HRTIM1_ApplyPfmStep(uint16_t per,
                         uint16_t cmpA,
                         uint16_t cmpB,
                         uint16_t cmpC,
                         uint16_t phaseB,
                         uint16_t phaseC);

/* Register access helpers */
volatile uint32_t *HRTIM1_GetMasterPerRegAddress(void);
volatile uint32_t *HRTIM1_GetMasterCmp1RegAddress(void);
volatile uint32_t *HRTIM1_GetMasterCmp2RegAddress(void);

volatile uint32_t *HRTIM1_GetTimerAPerRegAddress(void);
volatile uint32_t *HRTIM1_GetTimerBPerRegAddress(void);
volatile uint32_t *HRTIM1_GetTimerCPerRegAddress(void);

volatile uint32_t *HRTIM1_GetTimerACmp1RegAddress(void);
volatile uint32_t *HRTIM1_GetTimerBCmp1RegAddress(void);
volatile uint32_t *HRTIM1_GetTimerCCmp1RegAddress(void);

#ifdef __cplusplus
}
#endif

#endif /* __HRTIM_H__ */
