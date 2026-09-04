# WHAM-PFMG474-V4 — Serial Command Reference

USART2, **9600 8N1**. Commands are terminated by `\r`, `\n`, or `\r\n`.

## Response conventions

Ported from the sibling PFM-STM32G474 project, per project decision:

| Form | Meaning |
|---|---|
| `OK\r\n` | Accepted, no data |
| `OK <value>\r\n` | Accepted, with a return value |
| `ERR <n> <msg>\r\n` | Rejected; error codes are stable across firmware versions |

### Error codes

| Code | Meaning |
|---|---|
| 1 | Unknown command |

(Only one error code exists so far — this table grows as commands that
can fail in more specific ways are added. Codes are never renumbered or
reused once assigned, matching the sibling project's convention.)

## Mnemonic syntax (SCPI-style)

Commands are matched by `cmd_parser.c`'s `scpi_match()` against a flat
table of patterns — see that file's header comment for the full
specification. Summary:

- **Hierarchical**: pattern levels are separated by `:`, e.g.
  `SOURce:VOLTage:LIMit`. A leading `:` on the input is tolerated but
  never required (there's no "current path" concept — every command is
  matched from the root).
- **Short/long form, per level**: in a pattern token, the leading
  UPPERCASE run is the mandatory short form; any lowercase letters
  after it are an optional long-form suffix that must be matched *in
  full* if used at all — no partial-long-form matches. Example:
  `VERSion` accepts `VERS`, `VERSION`, `version` — not `VERSI`.
- **Case-insensitive** throughout.
- **Query suffix `?` must match exactly**: a pattern ending in `?` only
  matches input also ending in `?`, and vice versa.
- **Common (IEEE 488.2) commands** like `*IDN?` are just zero-colon
  patterns — no special-casing needed.

Compound commands (`;`-separated, multiple mnemonics on one line) are
**not** supported yet.

## Commands

### `*IDN?`

Board and firmware identification.

```
> *IDN?
< OK WHAM-PFMG474-V4 REVA v0.6
```

Reports, space-separated: `HW_BOARD_NAME`, `HW_BOARD_REV`,
`FW_VERSION_STRING` — all compile-time constants in `Core/Inc/version.h`.
`HW_BOARD_REV` is currently a placeholder (`REVA`); update it to match
the actual PCB silkscreen revision.

### `BOOT`

Resets the MCU into the STM32 ROM serial bootloader (System memory), so
a new `.bin` can be written over the same USART2 link with no physical
BOOT0/NRST access. See `docs/serial_reflash_guide.md` for the full
mechanism and usage via `python/wham_serial_flash.py`.

```
> BOOT
< OK ENTERING BOOTLOADER
  (link drops — MCU has reset into the ROM bootloader)
```

Present only when `BOOT_JUMP_FEATURE_ENABLED` (`Core/Inc/boot_jump.h`)
is nonzero (the default). When disabled, `BOOT` is simply unrecognized
(`ERR 1 Unknown command`), like any other unknown mnemonic — see that
header for exactly what disabling the module does.

No state-machine/Firing concept exists in this firmware yet, so `BOOT`
has no rejection conditions today. When application logic that can be
mid-operation is added, gate this command the same way the sibling
project's `cmd_boot()` does (reject with an `ERR` code while active —
resetting under load would drop outputs uncontrolled).

## Adding a command

From `cmd_parser.c`'s own header comment:

1. Implement the handler in `commands.c`.
2. Declare it in `commands.h`.
3. Add a `{ "PATTern:MNEMonic?", handler }` row to `command_table[]` in
   `cmd_parser.c`.

Nothing else changes — the table is flat, so a new leaf or a whole new
subsystem is always just one more row. Update this document when you do.
