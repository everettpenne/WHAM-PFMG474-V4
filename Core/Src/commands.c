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
#include <stdio.h>

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
