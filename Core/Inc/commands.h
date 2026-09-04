#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "uart.h"
#include "boot_jump.h"

/* --------------------------------------------------------------------------
 * Command handler implementations for WHAM-PFMG474-V4.
 *
 * Response conventions (ported from the sibling PFM-STM32G474 project,
 * per project decision):
 *   OK\r\n              - accepted, no data
 *   OK <value>\r\n      - accepted, with return value
 *   ERR <n> <msg>\r\n   - rejected; error codes are stable across versions
 *
 * Mnemonics are SCPI-style hierarchical patterns matched by
 * cmd_parser.c's scpi_match() -- see that file's header for the
 * short/long-form and multi-layer (':') matching rules.
 *
 * To add a command: declare its handler here, implement it in
 * commands.c, and add a row to cmd_parser.c's command_table[].
 * -------------------------------------------------------------------------- */

/* Identification / system */
void cmd_idn(uart_instance_t *inst, char *args); /* *IDN? -- board + firmware
                                                      identification, see
                                                      version.h */

/* Serial-bootloader entry. Gated on BOOT_JUMP_FEATURE_ENABLED
 * (boot_jump.h) -- entirely absent, including cmd_parser.c's "BOOT"
 * table row, when that module is disabled. */
#if (BOOT_JUMP_FEATURE_ENABLED != 0)
void cmd_boot(uart_instance_t *inst, char *args); /* BOOT -- reset into the
                                                       ROM serial bootloader,
                                                       see boot_jump.c */
#endif

#ifdef __cplusplus
}
#endif

#endif /* __COMMANDS_H__ */
