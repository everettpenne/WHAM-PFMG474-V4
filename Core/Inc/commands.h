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
 * Error codes
 * ------------
 *   1   Unknown command
 *   2   Not currently uploading a table -- send TABle:BEGin first
 *   3   Table full (PFM_TABLE_SIZE entries already appended)
 *   4   Invalid TABle:STEP arguments (wrong count, or a value outside
 *       uint16 range 0-65535)
 *   5   Table is empty -- FIRE has nothing to play back
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

/* PFM table upload -- a complete shot profile built entirely
 * off-controller (see python/pfm_table_upload.py) and streamed in one
 * step at a time. See pfm.h's "ADDED" header note and
 * docs/command_reference.md for the full protocol and why
 * construction deliberately lives on the host, not here. */
void cmd_table_begin(uart_instance_t *inst, char *args); /* TABle:BEGin -- clears
                                                              the table, opens an
                                                              upload session */
void cmd_table_step(uart_instance_t *inst, char *args);  /* TABle:STEP <per> <cmpA>
                                                              <cmpB> <cmpC> -- appends
                                                              one entry */
void cmd_table_end(uart_instance_t *inst, char *args);   /* TABle:END -- closes the
                                                              upload session, reports
                                                              the final entry count */
void cmd_table_query(uart_instance_t *inst, char *args); /* TABle? -- reports the
                                                              current entry count */

/* Begins PWM output: (re)starts playback of the currently-uploaded PFM
 * table from step 0 (PFM_Restart(), which also enables the HRTIM
 * channels for output -- see hrtim.c's HRTIM1_PWM_Start()). No ARM/
 * state-machine interlock exists in this minimal firmware -- FIRE
 * always takes effect immediately, whether idle or already mid-shot.
 * Rejects with ERR 5 if the table is empty (see commands.c). */
void cmd_fire(uart_instance_t *inst, char *args); /* FIRE -- start PWM output */

#ifdef __cplusplus
}
#endif

#endif /* __COMMANDS_H__ */
