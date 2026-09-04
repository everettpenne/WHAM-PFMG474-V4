/*
 * hrtim.c
 *
 * Ported from the sibling PFM-STM32G474 project's hrtim.c, unchanged in
 * behavior -- see hrtim.h for what was deliberately left out
 * (HRTIM1_EmergencyStop(), FEEDBACK-mode anything) and why.
 */

#include "hrtim.h"
#include <string.h>

HRTIM_HandleTypeDef hhrtim1;

static uint16_t HRTIM1_ClampCompare(uint16_t cmp, uint16_t per)
{
    uint16_t maxCmp;

    if (per <= 4U)
    {
        return HRTIM_COMPARE_MIN;
    }

    maxCmp = (uint16_t)(per - 2U);

    if (cmp < HRTIM_COMPARE_MIN)
    {
        cmp = HRTIM_COMPARE_MIN;
    }

    if (cmp > maxCmp)
    {
        cmp = maxCmp;
    }

    return cmp;
}

void HRTIM1_FullInit(void)
{
    HRTIM_TimeBaseCfgTypeDef pTimeBaseCfg;
    HRTIM_TimerCfgTypeDef pTimerCfg;
    HRTIM_CompareCfgTypeDef pCompareCfg;
    HRTIM_OutputCfgTypeDef pOutputCfg;
    HRTIM_DeadTimeCfgTypeDef pDeadTimeCfg;

    memset(&pTimeBaseCfg, 0, sizeof(pTimeBaseCfg));
    memset(&pTimerCfg, 0, sizeof(pTimerCfg));
    memset(&pCompareCfg, 0, sizeof(pCompareCfg));
    memset(&pOutputCfg, 0, sizeof(pOutputCfg));
    memset(&pDeadTimeCfg, 0, sizeof(pDeadTimeCfg));

    hhrtim1.Instance = HRTIM1;

    if (HAL_HRTIM_Init(&hhrtim1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_HRTIM_DLLCalibrationStart(&hhrtim1, HRTIM_CALIBRATIONRATE_3) != HAL_OK)
        Error_Handler();
    if (HAL_HRTIM_PollForDLLCalibration(&hhrtim1, 10) != HAL_OK)
        Error_Handler();

    /* Default initial period:
       100 kHz => 170000000 / 100000 - 1 = 1699 */
    pTimeBaseCfg.Period = 1699U;
    pTimeBaseCfg.RepetitionCounter = 0U;
    pTimeBaseCfg.PrescalerRatio = HRTIM_PRESCALERRATIO_DIV1;
    pTimeBaseCfg.Mode = HRTIM_MODE_CONTINUOUS;

    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    /* Timers D/E/F: same shared carrier period as A/B/C (all timers on
       this board's default init run at the same initial ~100 kHz) --
       see the ResetTrigger comment below for why they are NOT
       phase-locked to the Master despite sharing this period. */
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, &pTimeBaseCfg) != HAL_OK)
    {
        Error_Handler();
    }

    /* The Master timer's own MPER/MCMP1R/MCMP2R registers are written
       every cycle by HRTIM1_ApplyPfmStep(), exactly like Timer A/B/C's
       PERxR/CMP1xR. HAL_HRTIM_TimeBaseConfig() (called above) only
       configures counter mode/prescaler/period/repetition-counter -- it
       does NOT configure preload/update-trigger behavior for the
       Master, that is HAL_HRTIM_WaveformTimerConfig()'s job. Without an
       explicit, enabled preload and a defined update event (here: on
       the Master's own repetition event, matching the convention used
       for A/B/C), the Master's own PER/CMP1/CMP2 shadow-to-active
       transfer timing is undefined. A minimal HRTIM_TimerCfgTypeDef is
       used here (not the fully-populated pTimerCfg reused below for
       A/B/C) since fields like ResetTrigger/DeadTimeInsertion/
       FaultEnable don't apply to the Master timer. */
    {
        HRTIM_TimerCfgTypeDef masterTimerCfg;
        memset(&masterTimerCfg, 0, sizeof(masterTimerCfg));

        masterTimerCfg.InterruptRequests = HRTIM_MASTER_IT_NONE;
        masterTimerCfg.DMARequests = HRTIM_TIM_DMA_NONE;
        masterTimerCfg.HalfModeEnable = DISABLE;
        masterTimerCfg.StartOnSync = DISABLE;
        masterTimerCfg.ResetOnSync = DISABLE;
        masterTimerCfg.DACSynchro = HRTIM_DACSYNC_NONE;
        masterTimerCfg.PreloadEnable = HRTIM_PRELOAD_ENABLED;
        masterTimerCfg.UpdateGating = HRTIM_UPDATEGATING_INDEPENDENT;
        masterTimerCfg.BurstMode = HRTIM_TIMERBURSTMODE_MAINTAINCLOCK;
        masterTimerCfg.RepetitionUpdate = HRTIM_UPDATEONREPETITION_ENABLED;

        if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, &masterTimerCfg) != HAL_OK)
        {
            Error_Handler();
        }
    }

    pTimerCfg.InterruptRequests = HRTIM_MASTER_IT_NONE;
    pTimerCfg.DMARequests = HRTIM_TIM_DMA_NONE;
    pTimerCfg.HalfModeEnable = DISABLE;
    pTimerCfg.StartOnSync = DISABLE;
    pTimerCfg.ResetOnSync = DISABLE;
    pTimerCfg.DACSynchro = HRTIM_DACSYNC_NONE;
    pTimerCfg.PreloadEnable = HRTIM_PRELOAD_ENABLED;
    pTimerCfg.UpdateGating = HRTIM_UPDATEGATING_INDEPENDENT;
    pTimerCfg.BurstMode = HRTIM_TIMERBURSTMODE_MAINTAINCLOCK;
    pTimerCfg.RepetitionUpdate = HRTIM_UPDATEONREPETITION_ENABLED;
    pTimerCfg.PushPull = HRTIM_TIMPUSHPULLMODE_DISABLED;
    pTimerCfg.FaultEnable = HRTIM_TIMFAULTENABLE_NONE;
    pTimerCfg.DeadTimeInsertion = HRTIM_TIMDEADTIMEINSERTION_ENABLED;

    /* UpdateTrigger = MASTER ties each slave timer's shadow->active
       transfer to the Master timer's own update event, so A/B/C's new
       PER/CMP values become active at the same well-defined, coherent
       boundary as their reset (MASTER_PER/CMP1/CMP2 below). This
       matches ST's own official multiphase reference example
       (STM32CubeF3 HRTIM_Multiphase), which uses this exact
       reset-from-master architecture. */
    pTimerCfg.UpdateTrigger = HRTIM_TIMUPDATETRIGGER_MASTER;
    pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_NONE;
    pTimerCfg.ResetUpdate = HRTIM_TIMUPDATEONRESET_DISABLED;

    pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_MASTER_PER;
    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pTimerCfg) != HAL_OK)
        Error_Handler();

    pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_MASTER_CMP1;
    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pTimerCfg) != HAL_OK)
        Error_Handler();

    pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_MASTER_CMP2;
    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, &pTimerCfg) != HAL_OK)
        Error_Handler();

    /* Timers D/E/F: initialized (dead time, complementary outputs) the
       same as A/B/C below, but deliberately NOT given a Master-derived
       ResetTrigger -- the Master timer has exactly 4 compare units
       (MCMP1R-MCMP4R), enough for at most 5 Master-synchronized,
       evenly-spaced phases (PER + 4 CMPs), one short of the 6 needed
       for a true symmetric A-F hexagon. Rather than pick an arbitrary,
       unverified phase assignment for D/E/F, they're left with
       ResetTrigger = NONE (free-running from their own period, no
       defined phase relationship to A/B/C or each other) and
       UpdateTrigger = NONE (self-updating at their own repetition
       event via RepetitionUpdate, set above, rather than gated on the
       Master's). This makes them fully valid standalone complementary
       PWM channels if started -- just not phase-coordinated with
       anything else yet. See version.h's PWM_NUM_CHANNELS comment.
       Also per project decision (2026-09-04): HRTIM1_PWM_Start() is
       NOT extended to these three timers here -- they are configured,
       reserved, and pin-muxed, but nothing starts their counters. */
    pTimerCfg.UpdateTrigger = HRTIM_TIMUPDATETRIGGER_NONE;
    pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_NONE;

    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, &pTimerCfg) != HAL_OK)
        Error_Handler();

    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, &pTimerCfg) != HAL_OK)
        Error_Handler();

    if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, &pTimerCfg) != HAL_OK)
        Error_Handler();

    /* Initial compare ~50% */
    pCompareCfg.CompareValue = 850U;
    pCompareCfg.AutoDelayedMode = HRTIM_AUTODELAYEDMODE_REGULAR;
    pCompareCfg.AutoDelayedTimeout = 0U;

    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }

    /* Master compare values for 120 / 240 degrees at 100 kHz initial period --
       for A/B/C only; D/E/F have no Master-derived phase (see above). */
    pCompareCfg.CompareValue = 567U;
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }

    pCompareCfg.CompareValue = 1133U;
    if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, HRTIM_COMPAREUNIT_2, &pCompareCfg) != HAL_OK)
    {
        Error_Handler();
    }

    pDeadTimeCfg.Prescaler = HRTIM_TIMDEADTIME_PRESCALERRATIO_DIV1;
    pDeadTimeCfg.RisingValue = HRTIM_DEADTIME_COUNTS;
    pDeadTimeCfg.RisingSign = HRTIM_TIMDEADTIME_RISINGSIGN_POSITIVE;
    pDeadTimeCfg.RisingLock = HRTIM_TIMDEADTIME_RISINGLOCK_WRITE;
    pDeadTimeCfg.RisingSignLock = HRTIM_TIMDEADTIME_RISINGSIGNLOCK_WRITE;
    pDeadTimeCfg.FallingValue = HRTIM_DEADTIME_COUNTS;
    pDeadTimeCfg.FallingSign = HRTIM_TIMDEADTIME_FALLINGSIGN_POSITIVE;
    pDeadTimeCfg.FallingLock = HRTIM_TIMDEADTIME_FALLINGLOCK_WRITE;
    pDeadTimeCfg.FallingSignLock = HRTIM_TIMDEADTIME_FALLINGSIGNLOCK_WRITE;

    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, &pDeadTimeCfg) != HAL_OK)
    {
        Error_Handler();
    }

    /* Output Configuration:
       Timer A/B/C output 1: set at timer period, reset at timer CMP1

       Output 2 (the complement: UN/VN/WN) uses the SAME pOutputCfg as
       output 1 -- same Polarity, same SetSource, same ResetSource. This
       matches ST's own reference for DeadTimeInsertion=ENABLED timers
       (Examples/HRTIM/HRTIM_BuckBoost): when dead-time insertion is
       enabled on the timer (set above in pTimerCfg), the dead-time
       hardware unit automatically drives output 2 as the inverse of
       output 1 with the configured dead-time gap inserted between
       edges. You do NOT give output 2 a different SetSource/ResetSource
       to make it "complementary" -- the silicon does that inversion
       itself once DeadTimeInsertion is enabled; SetSource/ResetSource
       on output 2 in that mode is effectively ignored / re-derived from
       output 1's crossbar.
    */
    pOutputCfg.Polarity = HRTIM_OUTPUTPOLARITY_HIGH;
    pOutputCfg.SetSource = HRTIM_OUTPUTSET_TIMPER;
    pOutputCfg.ResetSource = HRTIM_OUTPUTRESET_TIMCMP1;
    pOutputCfg.IdleMode = HRTIM_OUTPUTIDLEMODE_NONE;
    pOutputCfg.IdleLevel = HRTIM_OUTPUTIDLELEVEL_INACTIVE;
    pOutputCfg.FaultLevel = HRTIM_OUTPUTFAULTLEVEL_NONE;
    pOutputCfg.ChopperModeEnable = DISABLE;
    pOutputCfg.BurstModeEntryDelayed = DISABLE;

    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_OUTPUT_TC1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, HRTIM_OUTPUT_TD1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, HRTIM_OUTPUT_TE1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, HRTIM_OUTPUT_TF1, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }

    /* Output 2: same pOutputCfg as output 1, per the ST reference cited
       above. Do NOT change Polarity, SetSource, or ResetSource here --
       see the comment block above this section for why. */
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_OUTPUT_TC2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_D, HRTIM_OUTPUT_TD2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_E, HRTIM_OUTPUT_TE2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_F, HRTIM_OUTPUT_TF2, &pOutputCfg) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_HRTIM_MspInit(HRTIM_HandleTypeDef *hhrtim)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    if (hhrtim->Instance == HRTIM1)
    {
        memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

        __HAL_RCC_HRTIM1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        /* PA8/9 = HRTIM1_CHA1/2 (PHASE_U/UN), PA10/11 = HRTIM1_CHB1/2
           (PHASE_V/VN) -- per docs/pin_mapping_v4.csv, unchanged from
           the sibling project's pinout for these 4 pins. */
        GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF13_HRTIM1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* PB12/13 = HRTIM1_CHC1/2 (PHASE_W/WN), PB14/15 = HRTIM1_CHD1/2
           (PHASE_X/XN, new on this board) -- both pairs use AF13, same
           as A/B/C. AF number confirmed against the STM32G474
           datasheet's Table 13 (Alternate function), column-position
           verified against PB12's own known-good AF13 entry as a
           calibration point -- see docs/changelog.txt. */
        GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF13_HRTIM1;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        /* PC6/7 = HRTIM1_CHF1/2 (PHASE_Z/ZN) -- AF13, same as the rest. */
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF13_HRTIM1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        /* PC8/9 = HRTIM1_CHE1/2 (PHASE_Y/YN) -- the ONE exception: this
           pair maps to AF3, not AF13, on this package. Confirmed via
           the same datasheet column-position method (verified twice,
           including cross-checking neighboring I2C3_SCL/SDA and
           TIM3_CH3/CH4 land on their own textbook-correct AF columns)
           -- do not "fix" this to match the other five pairs, it is
           deliberately different and a wrong AF here means the pin
           silently never connects to HRTIM1 at all. */
        GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF3_HRTIM1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    }
}

void HAL_HRTIM_MspDeInit(HRTIM_HandleTypeDef *hhrtim)
{
    if (hhrtim->Instance == HRTIM1)
    {
        __HAL_RCC_HRTIM1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11);
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9);
    }
}

/* Waits for one specific Master-timer flag (RESET/MCMP1/MCMP2) to go
 * true in hardware, then connects exactly one phase's outputs to the
 * pins -- see HRTIM1_PWM_Start() below for why this exists and why it
 * must be called with global interrupts already masked. `reforceActive`
 * re-asserts output1 ACTIVE/output2 INACTIVE (the same SETx1R/RSTx2R
 * "software trigger" bits HAL_HRTIM_WaveformSetOutputLevel() uses,
 * written directly for the same reason OENR is below: this call site
 * owns hhrtim1 exclusively while it runs, so the HAL's lock/state-machine
 * overhead is pure avoidable latency here) immediately before unmasking
 * -- needed for Timers B/C, whose ResetTrigger (MASTER_CMP1/CMP2) fires
 * BEFORE their own natural period rollover, so the external reset does
 * NOT itself generate a fresh SET event (confirmed against real hardware
 * data, see the call sites' comments) and the output would otherwise
 * still reflect whatever state the very first force-ACTIVE call (before
 * ANY counter started) left it in. NOT needed for Timer A, whose
 * ResetTrigger (MASTER_PER) coincides with its own natural rollover --
 * that IS a genuine TIMPER SET-source event, so the output already
 * transitions correctly on its own.
 *
 * Returns 0 on success, 1 if the spin-count ceiling was hit (a real
 * fault -- the counters aren't actually running -- not a timing corner
 * case; see HRTIM1_PWM_Start()'s comment on why this can't be a
 * wall-clock timeout here). */
static uint8_t HRTIM1_WaitForPhaseAndConnect(uint32_t masterFlag,
                                             uint32_t timerIdx,
                                             uint32_t output1,
                                             uint32_t output2,
                                             uint32_t outputMask,
                                             uint8_t reforceActive)
{
    uint32_t spins = 0U;

    while (__HAL_HRTIM_MASTER_GET_FLAG(&hhrtim1, masterFlag) == RESET)
    {
        spins++;
        if (spins >= 1000000U)
        {
            return 1U;
        }
    }

    if (outputMask != 0U)
    {
        if (reforceActive != 0U)
        {
            hhrtim1.Instance->sTimerxRegs[timerIdx].SETx1R |= HRTIM_SET1R_SST;
            hhrtim1.Instance->sTimerxRegs[timerIdx].RSTx2R |= HRTIM_RST2R_SRT;
            (void)output1;   /* named for readability at the call sites;
                                 the actual register write only needs
                                 timerIdx, not the HRTIM_OUTPUT_Tx1/2
                                 constants HAL's own API takes */
            (void)output2;
        }
        hhrtim1.Instance->sCommonRegs.OENR |= outputMask;
    }

    return 0U;
}

void HRTIM1_PWM_Start(uint8_t enableU, uint8_t enableV, uint8_t enableW)
{
    uint32_t outputMaskU = 0U;
    uint32_t outputMaskV = 0U;
    uint32_t outputMaskW = 0U;

    /* With DeadTimeInsertion enabled, output 2 of each timer is driven
       as the hardware-inverted complement of output 1 -- but there is
       no natural "first state" for a complementary pair coming out of
       reset. Per ST's HAL documentation: "when dead-time insertion is
       enabled it is necessary to force the output level by software to
       have the outputs in a complementary state as soon as the RUN
       mode is entered." Force output 1 ACTIVE / output 2 INACTIVE on
       each timer before starting the counters, so the pair begins in a
       known, genuinely complementary state rather than whatever level
       the deadtime unit happens to reset into. Only done for phases
       that will actually be enabled below -- harmless either way since
       WaveformSetOutputLevel doesn't itself enable an output, but no
       reason to touch a phase that's staying disabled.

       This is a PREREQUISITE for the counters to start into a defined
       state, not the last word on what the pins eventually show -- see
       HRTIM1_WaitForPhaseAndConnect()'s `reforceActive` for why V/W
       need this repeated later, right as they individually connect. */
    if (enableU != 0U)
    {
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA1, HRTIM_OUTPUTLEVEL_ACTIVE);
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA2, HRTIM_OUTPUTLEVEL_INACTIVE);
        outputMaskU = (HRTIM_OUTPUT_TA1 | HRTIM_OUTPUT_TA2);
    }
    if (enableV != 0U)
    {
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB1, HRTIM_OUTPUTLEVEL_ACTIVE);
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB2, HRTIM_OUTPUTLEVEL_INACTIVE);
        outputMaskV = (HRTIM_OUTPUT_TB1 | HRTIM_OUTPUT_TB2);
    }
    if (enableW != 0U)
    {
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_OUTPUT_TC1, HRTIM_OUTPUTLEVEL_ACTIVE);
        HAL_HRTIM_WaveformSetOutputLevel(&hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_OUTPUT_TC2, HRTIM_OUTPUTLEVEL_INACTIVE);
        outputMaskW = (HRTIM_OUTPUT_TC1 | HRTIM_OUTPUT_TC2);
    }

    /* Cold-start phase-lock settling -- per-phase, individually timed.
       The force-ACTIVE calls above put U/V/W's main outputs all HIGH at
       the same instant, with no regard for their intended 120/240 deg
       stagger -- necessary (per ST's own guidance, cited above) to
       avoid an undefined complementary-pair state, but it means the
       pins do NOT yet reflect each timer's real phase relationship.
       Timer A's ResetTrigger=MASTER_PER coincides with its own natural
       rollover; Timers B/C (ResetTrigger=MASTER_CMP1/CMP2) only become
       correctly phase-locked once they've received that first
       Master-CMPx-triggered reset, at 1/3 and 2/3 of a period.

       Two real-hardware findings (DSLogic captures, 2026-09-04) shaped
       this function twice:
         1. Without ANY delay, U/V/W's first-ever output edges all land
            on the exact same sample -- a garbled ~1-2 cycles before
            self-correcting. Fixed by not connecting outputs until
            phase-lock had settled.
         2. That first fix waited for a single event (one full Master
            period, via MREP) and connected ALL THREE outputs together
            -- which stopped the catastrophic collision, but exposed
            V/W mid-way through whatever their comparators had been
            doing since their OWN (earlier, still-hidden) MASTER_CMP1/
            CMP2 reset -- a real duty cycle, just not starting from a
            clean edge, and not reliably positioned at 120/240 deg
            either (what looked like a "recovery" edge near there was
            actually just V/C's own natural TIMPER rollover after their
            hidden reset, which lands there only by coincidence of the
            specific duty value -- it will NOT track 120/240 deg for
            other duty values). Confirmed by direct inspection of a
            captured .dsl file's raw channel data.

       Fix: wait for and connect each phase SEPARATELY, at ITS OWN
       reset-trigger event -- V at MASTER_CMP1 (1/3 of a period), W at
       MASTER_CMP2 (2/3), U at MASTER_PER/MREP (the full period, as
       before) -- with V/W's output re-forced ACTIVE at that exact
       moment (see HRTIM1_WaitForPhaseAndConnect()'s `reforceActive`).
       Each phase's first VISIBLE pulse is therefore a genuine fresh
       start referenced from ITS OWN phase point: nothing visible on
       V/W until 120/240 deg after U's first edge, then a clean,
       correctly-timed, correctly-dutied pulse -- not a snapshot of an
       already-in-progress cycle. Waiting on hardware flags rather than
       a computed busy-wait keeps this automatically correct for
       whatever period is currently loaded, same as before.

       All three flags are masked and cleared of stale state before the
       counters start, for the same reason as before: HRTIM1_Master_IRQn
       is armed from boot (or a previous shot) and must not react to any
       of these events until this function is done setting up the
       current one -- PFM_CycleBoundaryHandler() firing early would
       silently advance the table before step 0 was ever visible. */
    __HAL_HRTIM_MASTER_DISABLE_IT(&hhrtim1, HRTIM_MASTER_IT_MREP);
    __HAL_HRTIM_MASTER_CLEAR_IT(&hhrtim1, (HRTIM_MASTER_IT_MREP |
                                           HRTIM_MASTER_IT_MCMP1 |
                                           HRTIM_MASTER_IT_MCMP2));

    /* Counters for ALL THREE timers (and Master) always start,
       regardless of which phases are enabled -- a disabled phase's
       timer must stay running and synchronized with the Master via its
       ResetTrigger (MASTER_PER/CMP1/CMP2), or re-enabling it later
       would not be coherent with the other phases. Only the output
       pins themselves are gated per-phase below. */
    if (HAL_HRTIM_WaveformCounterStart(&hhrtim1,
                                       HRTIM_TIMERID_MASTER |
                                       HRTIM_TIMERID_TIMER_A |
                                       HRTIM_TIMERID_TIMER_B |
                                       HRTIM_TIMERID_TIMER_C) != HAL_OK)
    {
        Error_Handler();
    }

    /* Global interrupts masked for the entire settling sequence below
       (not just one flag/write) -- same reasoning as the single-event
       version this replaced: SysTick (priority 0, the HIGHEST in this
       firmware) or USART2 (priority 2) preempting any one of these
       three latency-sensitive windows would reintroduce exactly the
       jitter chasing this fix exists to remove. Bounded by each call's
       own 1,000,000-spin ceiling (HRTIM1_WaitForPhaseAndConnect()) --
       not a wall-clock timeout, since HAL_GetTick() cannot advance
       while global interrupts are masked (it's SysTick-driven, and
       SysTick is masked too). Total worst-case masked duration is still
       bounded by one Master period (~385 us worst case for a uint16_t
       `per`), same as before -- three sequential sub-waits within that
       same one-period budget, not three separate one-period waits. */
    __disable_irq();
    {
        uint8_t timedOut = 0U;

        /* V at 1/3 of a period (MASTER_CMP1) -- fires before W or U. */
        if (HRTIM1_WaitForPhaseAndConnect(HRTIM_MASTER_FLAG_MCMP1,
                                          HRTIM_TIMERINDEX_TIMER_B,
                                          HRTIM_OUTPUT_TB1, HRTIM_OUTPUT_TB2,
                                          outputMaskV, 1U) != 0U)
        {
            timedOut = 1U;
        }

        /* W at 2/3 of a period (MASTER_CMP2). */
        if (HRTIM1_WaitForPhaseAndConnect(HRTIM_MASTER_FLAG_MCMP2,
                                          HRTIM_TIMERINDEX_TIMER_C,
                                          HRTIM_OUTPUT_TC1, HRTIM_OUTPUT_TC2,
                                          outputMaskW, 1U) != 0U)
        {
            timedOut = 1U;
        }

        /* U at the full period (MASTER_PER/MREP) -- no reforceActive:
           this IS Timer A's own natural TIMPER SET event, not just an
           external reset, so the output already transitions correctly
           without help. */
        if (HRTIM1_WaitForPhaseAndConnect(HRTIM_MASTER_FLAG_MREP,
                                          HRTIM_TIMERINDEX_TIMER_A,
                                          HRTIM_OUTPUT_TA1, HRTIM_OUTPUT_TA2,
                                          outputMaskU, 0U) != 0U)
        {
            timedOut = 1U;
        }

        /* Not time-critical -- the interrupt just needs to be re-armed
           before this function returns, not before any of the connects
           above. Restores the always-armed state
           HRTIM1_EnableMasterInterrupt() established at boot; also
           clears whatever flags the three waits above just consumed. */
        __HAL_HRTIM_MASTER_CLEAR_IT(&hhrtim1, (HRTIM_MASTER_IT_MREP |
                                               HRTIM_MASTER_IT_MCMP1 |
                                               HRTIM_MASTER_IT_MCMP2));
        __HAL_HRTIM_MASTER_ENABLE_IT(&hhrtim1, HRTIM_MASTER_IT_MREP);

        if (timedOut != 0U)
        {
            __enable_irq();
            Error_Handler();
        }
    }
    __enable_irq();
    /* If outputMaskU/V/W are all 0 (every phase disabled), no outputs
       are connected at all -- counters run but nothing is ever driven.
       This is a legal, if unusual, state; the operator explicitly
       disabled every phase, so no output is exactly correct here. */
}

void HRTIM1_EnableMasterInterrupt(void)
{
    /* Priority scheme, matching the sibling PFM-STM32G474 project
       exactly (see FixSysTickPriority()'s doc comment in main.c):
       SysTick=0 (highest), HRTIM1_Master=1, USART2=2 (already set this
       way in MX_USART2_UART_Init()). Lower number = higher priority on
       this Cortex-M4's NVIC. */
    HAL_NVIC_SetPriority(HRTIM1_Master_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(HRTIM1_Master_IRQn);

    __HAL_HRTIM_MASTER_ENABLE_IT(&hhrtim1, HRTIM_MASTER_IT_MREP);
}

void HRTIM1_PWM_Stop(void)
{
    HAL_HRTIM_WaveformOutputStop(&hhrtim1,
                                  HRTIM_OUTPUT_TA1 |
                                  HRTIM_OUTPUT_TA2 |
                                  HRTIM_OUTPUT_TB1 |
                                  HRTIM_OUTPUT_TB2 |
                                  HRTIM_OUTPUT_TC1 |
                                  HRTIM_OUTPUT_TC2);

    /* Must also stop the underlying Master/A/B/C counters, not just the
       outputs -- see the sibling project's documented history: leaving
       the counters running after outputs stop means the Master
       repetition ISR keeps firing forever at the full carrier rate,
       which can starve the main loop (a blocking uart_send() call, for
       instance) indefinitely. Stopping the counters here (mirroring
       exactly which timers HRTIM1_PWM_Start() started) means the ISR
       genuinely stops firing once a shot ends. */
    HAL_HRTIM_WaveformCounterStop(&hhrtim1,
                                  HRTIM_TIMERID_MASTER |
                                  HRTIM_TIMERID_TIMER_A |
                                  HRTIM_TIMERID_TIMER_B |
                                  HRTIM_TIMERID_TIMER_C);
}

void HRTIM1_SoftwareUpdate(void)
{
    /* Force shadow→active transfer on Master + Timers A/B/C in a single
       CR2 write.  CR2 is the ONLY home of the software-update bits
       (MSWU/TxSWU) -- there is no per-timer equivalent in MCR/TIMxCR,
       which only configure update SOURCES.  Setting MSWU alone would
       also cascade to the slave timers (they are configured with
       UpdateTrigger = MASTER), but writing all four bits explicitly
       avoids any ambiguity about update-propagation order.  The bits
       are self-clearing in hardware -- they trigger exactly one update
       transfer and then reset to 0 without software intervention. */
    hhrtim1.Instance->sCommonRegs.CR2 |= (HRTIM_TIMERUPDATE_MASTER |
                                          HRTIM_TIMERUPDATE_A |
                                          HRTIM_TIMERUPDATE_B |
                                          HRTIM_TIMERUPDATE_C);
}

void HRTIM1_ApplyPfmStep(uint16_t per,
                         uint16_t cmpA,
                         uint16_t cmpB,
                         uint16_t cmpC,
                         uint16_t phaseB,
                         uint16_t phaseC)
{
    cmpA = HRTIM1_ClampCompare(cmpA, per);
    cmpB = HRTIM1_ClampCompare(cmpB, per);
    cmpC = HRTIM1_ClampCompare(cmpC, per);

    if (phaseB >= per)
    {
        phaseB = (uint16_t)(per / 3U);
    }

    if (phaseC >= per)
    {
        phaseC = (uint16_t)((2U * (uint32_t)per) / 3U);
    }

    /* Master period and phase offsets */
    *HRTIM1_GetMasterPerRegAddress() = per;
    *HRTIM1_GetMasterCmp1RegAddress() = phaseB;
    *HRTIM1_GetMasterCmp2RegAddress() = phaseC;

    /* All three subtimers share same period */
    *HRTIM1_GetTimerAPerRegAddress() = per;
    *HRTIM1_GetTimerBPerRegAddress() = per;
    *HRTIM1_GetTimerCPerRegAddress() = per;

    /* Duty per phase */
    *HRTIM1_GetTimerACmp1RegAddress() = cmpA;
    *HRTIM1_GetTimerBCmp1RegAddress() = cmpB;
    *HRTIM1_GetTimerCCmp1RegAddress() = cmpC;
}

volatile uint32_t *HRTIM1_GetMasterPerRegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sMasterRegs.MPER);
}

volatile uint32_t *HRTIM1_GetMasterCmp1RegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sMasterRegs.MCMP1R);
}

volatile uint32_t *HRTIM1_GetMasterCmp2RegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sMasterRegs.MCMP2R);
}

volatile uint32_t *HRTIM1_GetTimerAPerRegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[0].PERxR);
}

volatile uint32_t *HRTIM1_GetTimerBPerRegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[1].PERxR);
}

volatile uint32_t *HRTIM1_GetTimerCPerRegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[2].PERxR);
}

volatile uint32_t *HRTIM1_GetTimerACmp1RegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[0].CMP1xR);
}

volatile uint32_t *HRTIM1_GetTimerBCmp1RegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[1].CMP1xR);
}

volatile uint32_t *HRTIM1_GetTimerCCmp1RegAddress(void)
{
    return (volatile uint32_t *)&(HRTIM1->sTimerxRegs[2].CMP1xR);
}
