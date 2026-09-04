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
 *   NOT PORTED: the sibling project's SWEEP segment/track builder
 *             (PFM_BuildTable(), PFM_FreqInit/Seg, PFM_DutyInit_x/
 *             Seg_x, the interpolation math) and everything
 *             FEEDBACK-mode related. No on-controller construction
 *             logic exists, and none is planned -- see below.
 *
 *   ADDED (2026-09-04), NOT a port: PFM_TableReset()/PFM_AppendStep()/
 *             PFM_GetEntryCount() -- minimal, generic primitives so a
 *             serial command layer (commands.c's TABle:* commands) can
 *             write a complete table built entirely off-controller, by
 *             a host-side Python script (python/pfm_table_upload.py).
 *             Per project decision: table CONSTRUCTION lives on the
 *             host, not the firmware -- these three functions do no
 *             validation of what a step "means" (no frequency/duty
 *             math, no bounds tied to a supply type, none of that
 *             exists here), only bounds-checking that the table itself
 *             isn't overrun. commands.c's cmd_table_step() owns
 *             whatever validation is appropriate at the protocol
 *             layer. If a real on-controller builder is ever added
 *             later, it would use this exact same API.
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

/* --------------------------------------------------------------------------
 * Table upload primitives -- see the ADDED note in this file's header
 * comment. Generic on purpose: no notion of "upload session" state
 * (whether a TABLE:BEGIN was sent) lives here -- that's commands.c's
 * job, matching how the rest of this codebase keeps protocol-layer
 * state (e.g. cmd_boot()'s checks) out of the modules it calls into.
 * -------------------------------------------------------------------------- */

/* Empties the table (g_pfmEntryCount = 0) without touching HRTIM or
 * PFM_GetState() -- purely a data-structure reset, safe to call at any
 * time. Does NOT reset g_pfmIndex/g_holdCounter (PFM_ResetIndices()
 * already does that, separately, for the fault-clear/quiescent case;
 * this function is deliberately narrower). */
void PFM_TableReset(void);

/* Appends one entry at g_pfmTable[g_pfmEntryCount], then increments
 * g_pfmEntryCount. Returns 1 on success, 0 if the table is already at
 * PFM_TABLE_SIZE capacity (entry NOT written in that case -- the
 * caller's count of "how many actually got in" should stop advancing
 * on a 0 return, not silently keep calling). No range-checking on the
 * values themselves (per/cmpA/cmpB/cmpC accepted as given -- the
 * eventual apply-time HRTIM1_ClampCompare() in hrtim.c is the last
 * line of defense there, but callers should validate before this, not
 * rely on that clamp as anything other than a backstop). */
uint8_t PFM_AppendStep(uint16_t per, uint16_t cmpA, uint16_t cmpB, uint16_t cmpC);

/* Current g_pfmEntryCount -- how many entries PFM_AppendStep() has
 * actually written since the last PFM_TableReset(). */
uint16_t PFM_GetEntryCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __PFM_H__ */
