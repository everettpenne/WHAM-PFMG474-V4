/*
 * commands.c
 *
 * Command handler implementations for WHAM-PFMG474-V4.
 *
 * Still minimal by design -- *IDN and BOOT, on top of the ported
 * serial command architecture (uart.c + cmd_parser.c's tokenize/
 * dispatch, wired up in main.c/stm32g4xx_it.c). Add more handlers
 * here as they're needed, following cmd_parser.c's "to add a
 * command" recipe.
 */

#include "commands.h"
#include "uart.h"
#include "version.h"
#include "boot_jump.h"
#include "pfm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SendErr(uart_instance_t *inst, int code, const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "ERR %d %s\r\n", code, msg);
    uart_send(inst, buf);
}

/* --------------------------------------------------------------------------
 * Identification / system
 * -------------------------------------------------------------------------- */

void cmd_idn(uart_instance_t *inst, char *args)
{
    char buf[64];
    (void)args;

    /* Board + firmware identity in one line, matching the sibling
       PFM-STM32G474 project's *IDN convention (OK <value>, space-
       separated fields) -- see version.h for the constants. */
    snprintf(buf, sizeof(buf), "OK %s %s %s\r\n",
             HW_BOARD_NAME, HW_BOARD_REV, FW_VERSION_STRING);
    uart_send(inst, buf);
}

#if (BOOT_JUMP_FEATURE_ENABLED != 0)
void cmd_boot(uart_instance_t *inst, char *args)
{
    (void)args;

    /* No state-machine/Firing concept exists in this minimal firmware
       yet -- when one is added, gate this the same way the sibling
       PFM-STM32G474 project's cmd_boot() does (reject with ERR while
       Firing; resetting under load would drop outputs uncontrolled). */

    /* uart_send() is blocking (HAL_UART_Transmit with HAL_MAX_DELAY), so
       this ACK is guaranteed to be fully on the wire before
       BootJump_RequestBootloader() resets the MCU below -- the operator
       (or a flashing script) sees "OK ENTERING BOOTLOADER" before the
       link drops. */
    uart_send(inst, "OK ENTERING BOOTLOADER\r\n");

    BootJump_RequestBootloader();
    /* Never returns. */
}
#endif /* BOOT_JUMP_FEATURE_ENABLED */

/* --------------------------------------------------------------------------
 * PFM table upload
 *
 * TABle:BEGin -> zero or more TABle:STEP <per> <cmpA> <cmpB> <cmpC> ->
 * TABle:END. Construction (what the actual per/cmp values for a given
 * shot profile should be) happens entirely off-controller, in
 * python/pfm_table_upload.py -- this layer just accepts whatever
 * PFM_Step_t values it's given and writes them in, one at a time. See
 * pfm.h's "ADDED" header note for why that split was a deliberate
 * project decision, not a placeholder for a builder that's coming later.
 *
 * g_tableUploadActive is deliberately local to this file, not pfm.c --
 * "is an upload session open" is protocol-layer state, not table
 * data, matching how the rest of this codebase keeps that kind of
 * bookkeeping in commands.c (see cmd_boot()'s own comments for the
 * same principle applied elsewhere).
 * -------------------------------------------------------------------------- */
static uint8_t g_tableUploadActive = 0U;

void cmd_table_begin(uart_instance_t *inst, char *args)
{
    (void)args;

    PFM_TableReset();
    g_tableUploadActive = 1U;

    uart_send(inst, "OK\r\n");
}

void cmd_table_step(uart_instance_t *inst, char *args)
{
    char *tok;
    long  vals[4];
    int   n = 0;

    if (g_tableUploadActive == 0U)
    {
        SendErr(inst, 2, "Not currently uploading -- send TABLE:BEGIN first");
        return;
    }

    if (args == NULL)
    {
        SendErr(inst, 4, "TABLE:STEP needs exactly 4 values: per cmpA cmpB cmpC");
        return;
    }

    for (tok = strtok(args, " \r\n"); tok != NULL; tok = strtok(NULL, " \r\n"))
    {
        long v;

        if (n >= 4)
        {
            /* A 5th token showed up -- too many values, not a valid step. */
            n++;
            break;
        }

        v = atol(tok);
        if (v < 0L || v > 65535L)
        {
            SendErr(inst, 4, "Value out of uint16 range (0-65535)");
            return;
        }

        vals[n] = v;
        n++;
    }

    if (n != 4)
    {
        SendErr(inst, 4, "TABLE:STEP needs exactly 4 values: per cmpA cmpB cmpC");
        return;
    }

    if (PFM_AppendStep((uint16_t)vals[0], (uint16_t)vals[1],
                       (uint16_t)vals[2], (uint16_t)vals[3]) == 0U)
    {
        SendErr(inst, 3, "Table full");
        return;
    }

    uart_send(inst, "OK\r\n");
}

void cmd_table_end(uart_instance_t *inst, char *args)
{
    char buf[48];
    (void)args;

    g_tableUploadActive = 0U;

    snprintf(buf, sizeof(buf), "OK %u\r\n", (unsigned int)PFM_GetEntryCount());
    uart_send(inst, buf);
}

void cmd_table_query(uart_instance_t *inst, char *args)
{
    char buf[48];
    (void)args;

    snprintf(buf, sizeof(buf), "OK %u\r\n", (unsigned int)PFM_GetEntryCount());
    uart_send(inst, buf);
}

/* --------------------------------------------------------------------------
 * FIRE
 *
 * Begins PWM output by (re)starting playback of whatever table is
 * currently uploaded, from step 0. No ARM/state-machine interlock
 * exists in this minimal firmware -- FIRE always takes effect
 * immediately, whether the controller was idle or already mid-shot
 * (PFM_Restart() is safe to call in either case: it always resets to
 * step 0 and re-applies HRTIM1_PWM_Start()). If a fuller state machine
 * (ARM/interlock/fault gating, matching the sibling PFM-STM32G474
 * project's SM_Fire()) is ever needed here, this is the call site to
 * extend, not replace.
 *
 * The one guard: firing an empty table. PFM_CycleBoundaryHandler()
 * (the ISR-driven playback advance, see stm32g4xx_it.c's
 * HRTIM1_Master_IRQHandler()) already defends against g_pfmEntryCount
 * == 0 by stopping outputs again at the very first master-repetition
 * interrupt -- but that would happen silently, after a near-instant
 * blip on the outputs, with no error ever reported to the operator.
 * Rejecting it here instead gives a clear reason up front.
 * -------------------------------------------------------------------------- */
void cmd_fire(uart_instance_t *inst, char *args)
{
    (void)args;

    if (PFM_GetEntryCount() == 0U)
    {
        SendErr(inst, 5, "Table is empty -- upload one first (TABLE:BEGIN/STEP/END)");
        return;
    }

    PFM_Restart();

    uart_send(inst, "OK\r\n");
}
