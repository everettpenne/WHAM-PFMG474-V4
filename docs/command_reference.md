# WHAM-PFMG474-V4 — Serial Command Reference

USART2, **115200 8N1** (raised from 9600 on 2026-09-04 — see `docs/changelog.txt`; WHAM-PFMG474-V4-only, the sibling PFM-STM32G474 project still uses 9600). Commands are terminated by `\r`, `\n`, or `\r\n`.

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
| 2 | Not currently uploading a table — send `TABLE:BEGIN` first |
| 3 | Table full (`PFM_TABLE_SIZE` entries already appended) |
| 4 | Invalid `TABLE:STEP` arguments (wrong count, or a value outside uint16 range 0-65535) |
| 5 | Table is empty — `FIRE` has nothing to play back |

Codes are never renumbered or reused once assigned, matching the
sibling project's convention.

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

### `TABLE:BEGIN`, `TABLE:STEP`, `TABLE:END`, `TABLE?`

Uploads a complete PFM shot profile (a sequence of `(per, cmpA, cmpB,
cmpC)` steps — see `Core/Inc/pfm.h`'s `PFM_Step_t`), built entirely
off-controller. This firmware has no on-device table *construction*
logic (no frequency/duty math, no sweep builder) — see `pfm.h`'s
"ADDED" header note for why that's a deliberate project decision, not
a gap waiting to be filled in here. `python/pfm_table_upload.py` is
the reference host-side builder.

```
> TABLE:BEGIN
< OK
> TABLE:STEP 1699 850 850 850
< OK
> TABLE:STEP 1699 850 850 850
< OK
  ... (repeat for every entry) ...
> TABLE:END
< OK 500
```

- **`TABLE:BEGIN`** — clears the table (`PFM_TableReset()`) and opens
  an upload session. Safe to call again mid-upload to start over.
- **`TABLE:STEP <per> <cmpA> <cmpB> <cmpC>`** — appends exactly one
  entry. All four values are `uint16_t` (0-65535); no other validation
  happens here (see `PFM_AppendStep()`'s doc comment — the eventual
  apply-time `HRTIM1_ClampCompare()` in `hrtim.c` is a backstop, not a
  substitute for the host script sending sane values). Rejects with
  `ERR 2` if no `TABLE:BEGIN` is open, `ERR 3` if the table is already
  at `PFM_TABLE_SIZE` (5000) capacity, `ERR 4` for a malformed line.
- **`TABLE:END`** — closes the upload session and reports the final
  entry count (`OK <count>`). Table stays exactly as uploaded even if
  you never send this — it just stops enforcing "must call
  `TABLE:BEGIN` first" for a stray `TABLE:STEP`.
- **`TABLE?`** — reports the current entry count at any time
  (`OK <count>`), upload session open or not.

### `FIRE`

Begins PWM output: (re)starts playback of whatever table is currently
uploaded, from step 0. Playback advances automatically, one table
entry per PWM period, driven by the HRTIM1 master-repetition interrupt
(`HRTIM1_Master_IRQHandler()` in `stm32g4xx_it.c`, calling
`PFM_CycleBoundaryHandler()` in `pfm.c`) — no polling, no further
commands needed once fired. Output stops automatically, at a coherent
period boundary, once the last table entry has completed.

```
> FIRE
< OK
  (HRTIM channels A/B/C begin switching — see hrtim.c's
   HRTIM1_PWM_Start(); D/E/F are not started, see hrtim.h)
  ... plays back every uploaded (per, cmpA, cmpB, cmpC) entry in
      order, one period each ...
  (output stops on its own after the last entry — no further command
   or reply marks this; poll TABLE? or scope the outputs)
```

- No `ARM`/state-machine interlock exists in this firmware — `FIRE`
  always takes effect immediately, whether the controller was idle or
  already mid-shot (re-firing mid-shot restarts from step 0). If a
  fuller interlock/fault-gated firing sequence is ever needed, this is
  the command to extend, not a design decision this entry documents as
  final.
- Rejects with `ERR 5` if the table is empty (`TABLE?` reports 0).
  Without this check, an empty-table `FIRE` would still briefly enable
  outputs at step 0's (garbage, never-written) register contents before
  the very first master-repetition interrupt stopped them again —
  `ERR 5` catches this before it can happen at all.
- No `STOP` command exists yet — playback only stops on its own, at
  table exhaustion. Adding an operator-initiated stop is future work.

## Adding a command

From `cmd_parser.c`'s own header comment:

1. Implement the handler in `commands.c`.
2. Declare it in `commands.h`.
3. Add a `{ "PATTern:MNEMonic?", handler }` row to `command_table[]` in
   `cmd_parser.c`.

Nothing else changes — the table is flat, so a new leaf or a whole new
subsystem is always just one more row. Update this document when you do.
