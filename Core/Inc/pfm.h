#ifndef __PFM_H__
#define __PFM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/*
 * pfm.h / pfm.c
 *
 * Ported from the sibling PFM-STM32G474 project's pfm.c -- the table
 * PLAYBACK engine only, per project decision (2026-08-31):
 *
 *   PORTED:   PFM_Step_t, the table itself, and everything that walks
 *             it and writes HRTIM registers each cycle (PFM_Init(),
 *             PFM_ApplyCurrentStep(), PFM_CycleBoundaryHandler(),
 *             PFM_Restart(), PFM_ResetIndices(), PFM_GetState(),
 *             PFM_GetCurrentStep(), PFM_SetPhaseEnabled()/
 *             PFM_GetPhaseEnabled(), PFM_Phase120()/PFM_Phase240()).
 *
 *   NOT PORTED: anything that BUILDS a table -- the sibling project's
 *             SWEEP segment/track builder (PFM_BuildTable(),
 *             PFM_FreqInit/Seg, PFM_DutyInit_x/Seg_x, the interpolation
 *             math, TBL_BEGIN/DATA/END raw upload), and everything
 *             FEEDBACK-mode related. This means: as ported, the table
 *             starts and stays EMPTY (g_pfmEntryCount == 0) -- nothing
 *             in this pass populates it. PFM_CycleBoundaryHandler()'s
 *             own defensive check for that case stops the shot
 *             immediately (see pfm.c) rather than running on
 *             uninitialized entries, so this is a safe, inert default,
 *             not a silent hazard -- but a FIRE with no table builder
 *             wired up yet will do nothing. Populating the table (even
 *             minimally, e.g. a single fixed-frequency entry) is
 *             deliberately left as a separate future step.
 *
 * Also not ported: PFM_SetStartupFreq()/Duty()/PulseLength() and their
 * g_startup* state -- those exist in the sibling project purely to feed
 * the table builder (SET FREQ/DUTY/PULSE commands write them, and the
 * builder falls back to them), so they have no purpose here without a
 * builder to feed. Same reasoning for supply_config.h's ACTIVE_*
 * default macros -- this project doesn't have a SUPPLY_TYPE concept at
 * all yet.
 */

/* --------------------------------------------------------------------------
 * Hold periods
 * Number of PWM cycles to hold each table entry before stepping to next
   1 = update every period
   N = update every N periods.
 * -------------------------------------------------------------------------- */
#define PFM_HOLD_PERIODS                         (1U)

/* --------------------------------------------------------------------------
 * Table size. The sibling project derives this from its own supply_config.h
 * (SUPPLY_PFM_TABLE_SIZE, currently 5000 there, chosen for a specific RAM
 * budget history tied to features not present here) -- defined directly
 * here instead, at the same value, since there's no equivalent config
 * source yet. Revisit if/when this project grows its own RAM-budget
 * pressure worth tracking. PFM_Step_t is 8 bytes, so this sizes to
 * PFM_TABLE_SIZE * 8 bytes of static RAM regardless of how many entries
 * are ever actually populated.
 * -------------------------------------------------------------------------- */
#define PFM_TABLE_SIZE                           (5000U)

/* --------------------------------------------------------------------------
 * PFM_Step_t -- one playback entry. 8 bytes -- see the sibling project's
 * pfm.h for the RAM-budget history behind this exact layout (phaseB/
 * phaseC/freqHz were removed in favor of recomputing them from `per` at
 * apply time; ported here already in that shrunk form).
 * -------------------------------------------------------------------------- */
typedef struct
{
    uint16_t per;
    uint16_t cmpA;
    uint16_t cmpB;
    uint16_t cmpC;
} PFM_Step_t;

/* Pure functions of `per`: the 120°/240° master-timer phase offsets for
 * a 3-phase step at this period. */
uint16_t PFM_Phase120(uint16_t per);
uint16_t PFM_Phase240(uint16_t per);

typedef enum
{
    PFM_STATE_RUNNING = 0U,
    PFM_STATE_STOPPED
} PFM_State_t;

typedef enum
{
    PFM_PHASE_U = 0U,
    PFM_PHASE_V,
    PFM_PHASE_W
} PFM_Phase_t;

void PFM_Init(void);
void PFM_ApplyCurrentStep(void);
void PFM_CycleBoundaryHandler(void);
void PFM_Restart(void);          /* re-arm from external trigger: resets indices, applies step 0, starts outputs */
void PFM_ResetIndices(void);     /* quiescent index reset only, no register/output writes (e.g. fault clear) */
PFM_State_t PFM_GetState(void);  /* optional, for debug/status */

/* Per-phase output enable -- standalone, mode-independent, durable
 * state. Not reset by PFM_Init() or PFM_ResetIndices() -- only changes
 * via an explicit call to PFM_SetPhaseEnabled(). Defaults to all
 * enabled. */
void PFM_SetPhaseEnabled(PFM_Phase_t phase, uint8_t enabled);
uint8_t PFM_GetPhaseEnabled(PFM_Phase_t phase);

const PFM_Step_t *PFM_GetCurrentStep(void);
const PFM_Step_t *PFM_GetStepByIndex(uint16_t index);

#ifdef __cplusplus
}
#endif

#endif /* __PFM_H__ */
