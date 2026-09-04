/*
 * pfm.c
 *
 * Table playback engine, ported from the sibling PFM-STM32G474 project
 * -- see pfm.h's header comment for exactly what was and wasn't ported
 * (construction/builders and FEEDBACK mode were deliberately left out).
 */

#include "pfm.h"
#include "hrtim.h"

static PFM_Step_t g_pfmTable[PFM_TABLE_SIZE];
static uint16_t g_pfmIndex = 0U;
static uint16_t g_holdCounter = 0U;

/* Number of ENTRIES ACTUALLY BUILT in g_pfmTable[], as opposed to
   PFM_TABLE_SIZE (the fixed buffer capacity). With no table builder
   ported (see pfm.h), this starts at 0 and stays 0 -- nothing in this
   file ever increments it. PFM_CycleBoundaryHandler() must check
   g_pfmIndex against THIS, not PFM_TABLE_SIZE -- a short table would
   otherwise never trigger exhaustion, since the index would have to
   climb all the way to PFM_TABLE_SIZE first. */
static uint16_t g_pfmEntryCount = 0U;

static PFM_State_t g_pfmState = PFM_STATE_RUNNING;

/* --------------------------------------------------------------------------
 * Per-phase output enable
 *
 * Standalone state, independent of table construction/playback -- set
 * directly by whatever future command layer adds SET ENABLE/DISABLE,
 * and read by PFM_Restart() whenever a shot actually fires. Deliberately
 * NOT reset by PFM_Init() or PFM_ResetIndices() -- per the sibling
 * project's design decision (carried over here), phase enable/disable
 * is durable and only changes via an explicit call to
 * PFM_SetPhaseEnabled(). All three default to enabled.
 * -------------------------------------------------------------------------- */
static uint8_t g_phaseEnabledU = 1U;
static uint8_t g_phaseEnabledV = 1U;
static uint8_t g_phaseEnabledW = 1U;

/* Pure functions of `per`: the 120°/240° master-timer phase offsets for
 * a 3-phase step at this period. */
uint16_t PFM_Phase120(uint16_t per)
{
    uint32_t counts = (uint32_t)per + 1U;
    return (uint16_t)(counts / 3U);
}

uint16_t PFM_Phase240(uint16_t per)
{
    uint32_t counts = (uint32_t)per + 1U;
    return (uint16_t)((2U * counts) / 3U);
}

void PFM_Init(void)
{
    /* No table builder is wired up yet (see pfm.h) -- this just brings
       the module to a known-empty, known-quiescent state. */
    g_pfmIndex = 0U;
    g_holdCounter = 0U;
    g_pfmState = PFM_STATE_STOPPED;
    g_pfmEntryCount = 0U;
}

/* Last step actually written to HRTIM by PFM_ApplyCurrentStep(), used to
   skip redundant register writes -- most ISR periods in a real shot
   re-apply an unchanged step (a CONSTANT-style table is many identical
   entries), so most periods would otherwise re-clamp and rewrite all
   nine HRTIM registers with values they already hold. Six halfword
   compares replace that redundant apply on every unchanged period.
   Skipping is safe because the HRTIM preload (shadow) registers retain
   the last written values between update events -- rewriting an
   identical value is a pure no-op electrically.

   g_lastAppliedValid is cleared by PFM_Restart() so the first period of
   every shot always writes, regardless of what any previous shot left
   in the registers. */
static PFM_Step_t g_lastAppliedStep;
static uint8_t    g_lastAppliedValid = 0U;

void PFM_ApplyCurrentStep(void)
{
    const PFM_Step_t *pStep = &g_pfmTable[g_pfmIndex];

    /* phaseB/phaseC are not stored in PFM_Step_t -- they're a pure
       function of pStep->per, so the duplicate-write check below only
       needs to compare the four stored fields: if per matches, the
       recomputed phase will always match too, by construction. */
    if ((g_lastAppliedValid != 0U) &&
        (pStep->per  == g_lastAppliedStep.per)  &&
        (pStep->cmpA == g_lastAppliedStep.cmpA) &&
        (pStep->cmpB == g_lastAppliedStep.cmpB) &&
        (pStep->cmpC == g_lastAppliedStep.cmpC))
    {
        return;   /* registers already hold exactly these values */
    }

    g_lastAppliedStep  = *pStep;
    g_lastAppliedValid = 1U;

    HRTIM1_ApplyPfmStep(pStep->per,
                        pStep->cmpA,
                        pStep->cmpB,
                        pStep->cmpC,
                        PFM_Phase120(pStep->per),
                        PFM_Phase240(pStep->per));
}

void PFM_CycleBoundaryHandler(void)
{
    /* Defensive: with no table builder wired up (see pfm.h),
       g_pfmEntryCount is always 0 today, so this always takes the stop
       path below on the very first call after a shot starts -- applying
       g_pfmTable[0] of a never-built table would otherwise silently run
       on garbage. This is intentionally the same defensive check the
       sibling project uses, kept ready for the day a builder populates
       real entries. */
    if (g_pfmEntryCount == 0U)
    {
        HRTIM1_PWM_Stop();
        g_pfmState = PFM_STATE_STOPPED;
        return;
    }

    PFM_ApplyCurrentStep();

    g_holdCounter++;
    if (g_holdCounter >= PFM_HOLD_PERIODS)
    {
        g_holdCounter = 0U;
        g_pfmIndex++;

        /* Compare against g_pfmEntryCount (the ACTUAL built length),
           not PFM_TABLE_SIZE (the fixed buffer capacity) -- a short
           table would otherwise never be seen as exhausted here. */
        if (g_pfmIndex >= g_pfmEntryCount)
        {
            /* Last entry just completed this cycle.
               Stop outputs now, at a coherent boundary. */
            HRTIM1_PWM_Stop();
            g_pfmIndex = 0U;
            g_pfmState = PFM_STATE_STOPPED;
        }
    }
}

void PFM_Restart(void)
{
    g_pfmIndex = 0U;
    g_holdCounter = 0U;
    g_pfmState = PFM_STATE_RUNNING;

    /* Every shot must write HRTIM on its first period, no matter what a
       previous shot left in the registers. */
    g_lastAppliedValid = 0U;

    PFM_ApplyCurrentStep();   /* preload step 0 before starting */

    /* Cold-start fix: PFM_ApplyCurrentStep() (above) wrote the step-0
       PER/CMP/phase values to HRTIM shadow registers. With preload
       enabled, those writes do not take effect until the next update
       event -- but the counters have not started yet, so there has
       been no update event, and the active registers still hold the
       stale init defaults from HRTIM1_FullInit() (100 kHz, 50 % duty,
       zero phase offset). Without the software update below, the FIRST
       period of every shot would run with those wrong init values. The
       software update forces an immediate shadow→active transfer so
       the counters, when started one line below, begin their very
       first period with the correct step-0 values. */
    HRTIM1_SoftwareUpdate();

    HRTIM1_PWM_Start(g_phaseEnabledU, g_phaseEnabledV, g_phaseEnabledW);
}

void PFM_SetPhaseEnabled(PFM_Phase_t phase, uint8_t enabled)
{
    switch (phase)
    {
        case PFM_PHASE_U:
            g_phaseEnabledU = (enabled != 0U) ? 1U : 0U;
            break;
        case PFM_PHASE_V:
            g_phaseEnabledV = (enabled != 0U) ? 1U : 0U;
            break;
        case PFM_PHASE_W:
            g_phaseEnabledW = (enabled != 0U) ? 1U : 0U;
            break;
        default:
            break;
    }
}

uint8_t PFM_GetPhaseEnabled(PFM_Phase_t phase)
{
    switch (phase)
    {
        case PFM_PHASE_U:
            return g_phaseEnabledU;
        case PFM_PHASE_V:
            return g_phaseEnabledV;
        case PFM_PHASE_W:
            return g_phaseEnabledW;
        default:
            return 0U;
    }
}

void PFM_ResetIndices(void)
{
    /* Quiescent reset: zero g_pfmIndex/g_holdCounter WITHOUT touching
       HRTIM outputs or applying any step. Distinct from PFM_Restart(),
       which is for actually beginning a shot and does start outputs. */
    g_pfmIndex = 0U;
    g_holdCounter = 0U;
    g_pfmState = PFM_STATE_STOPPED;
}

PFM_State_t PFM_GetState(void)
{
    return g_pfmState;
}

const PFM_Step_t *PFM_GetCurrentStep(void)
{
    return &g_pfmTable[g_pfmIndex];
}

const PFM_Step_t *PFM_GetStepByIndex(uint16_t index)
{
    if (index >= PFM_TABLE_SIZE)
    {
        index = 0U;
    }

    return &g_pfmTable[index];
}
