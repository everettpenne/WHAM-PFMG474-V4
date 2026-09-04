# AGENTS.md — WHAM-PFMG474-V4 Firmware

Orientation file for any AI agent or LLM working in this repository.
Read this before touching code.

## What this project is

Firmware for a **new PCB respin** of the PFM-STM32G474 controller family,
on the same STM32G474QET6 (Cortex-M4F, 170 MHz, LQFP128) MCU. It is a
**deliberate ground-up minimal rewrite**, not a fork of the sibling
project's full source tree: only the pin/board-agnostic *architecture* is
being ported over piece by piece, verified, and built back up from a
clean base -- not the PFM/HRTIM/feedback-control application logic
itself (none of that exists here yet).

**Sibling project**: `../../PFM-STM32G474` (referred to below as "V3")
and `../../PFM-STM32G474-V4` ("V4") are separate git repositories for
the earlier hardware revision(s), further along and much larger in
scope (PWM generation, closed-loop feedback, fault handling, a GUI).
This project intentionally does not share git history with them. Code
ported over is called out explicitly below and in each file's own
comments -- check there before assuming a mechanism is unique to this
project or before "fixing" something that was a deliberate port.

## Current functionality (as of 2026-08-31)

Everything today is serial-command infrastructure. No PWM, no HRTIM,
no application-specific logic yet.

- **Serial command architecture** (`uart.c`, `cmd_parser.c`) -- ported
  from the sibling project, ported *architecture-only* (no command
  handlers came with it). Interrupt-driven single-byte USART2 RX with
  line-buffer accumulation (`uart.c`), polled from the main loop.
  Dispatch is a **flat, SCPI-style hierarchical command table**
  (`cmd_parser.c`) -- see `docs/command_reference.md` for the full
  mnemonic-matching rules (short/long form, `:`-separated levels, `?`
  query suffix). This is a genuine departure from the sibling project's
  case-sensitive exact-match dispatch; see that file's header comment
  for the reasoning.
- **`*IDN?`** (`commands.c`, constants in `version.h`) -- reports board
  name, hardware revision, and firmware version. `HW_BOARD_REV` in
  `version.h` is a placeholder; keep it in sync with the actual PCB
  silkscreen.
- **`BOOT`** / serial-bootloader entry (`boot_jump.c`) -- software jump
  into the STM32 ROM bootloader over USART2, no physical BOOT0/NRST
  access needed. Ported from the sibling project's `boot_jump.c`, with
  two additions made here:
  1. **Removable by design**: `BOOT_JUMP_FEATURE_ENABLED` in
     `boot_jump.h` is the single point of control -- see that header's
     doc comment for exactly what disabling it does and does not remove.
  2. **A real bugfix, found and confirmed on hardware in this project**
     (not present in the sibling project's copy as of this writing --
     see "Known gaps" below): the ROM bootloader's `Go` command (what
     `stm32flash -g` issues after writing a new image) does **not**
     perform a real chip reset, so `SCB->VTOR` and `SYSCFG->MEMRMP` can
     be left pointing at the bootloader's own vector table. Without a
     fix, the newly-flashed app runs but is deaf to every
     interrupt-driven peripheral (including the USART2 RX interrupt
     everything here depends on) until something -- a manual reset --
     clears it. `BootJump_CheckAndEnter()` now restores both
     unconditionally on the normal-boot path. See that function's doc
     comment for the full mechanism, and `docs/changelog.txt` for the
     debugging history (this was chased down empirically, on real
     hardware, over several flash/reset cycles).
- **Host-side tooling** (`python/`):
  - `wham_serial_flash.py` -- one-command serial reflash (`BOOT` +
    `stm32flash`). Ported from the sibling project's
    `pfm_serial_flash.py`, corrected for this project's actual baud and
    build artifact name. **Verified working end-to-end on real hardware**,
    including the VTOR/MEMRMP fix above (confirmed: flash a new version,
    query `*IDN?` immediately, no manual reset, get the new version back
    -- done twice, back to back).
  - `scpi.py` -- an interactive serial terminal (not written by an
    agent; predates this documentation pass). Useful for manual
    poking at the command set.

## Tech stack & layout

- C11, STM32Cube HAL (G4), bare metal -- no RTOS, no heap.
- Built with STM32CubeIDE's generated makefile; toolchain
  arm-none-eabi-gcc 13.3.
- `Core/Src|Inc/` -- all project code. Currently: `main.c`, `uart.c`,
  `cmd_parser.c`, `commands.c`, `boot_jump.c`, plus CubeMX-generated
  `stm32g4xx_hal_msp.c`/`stm32g4xx_it.c`/`system_stm32g4xx.c`/
  `syscalls.c`/`sysmem.c`.
- `python/` -- host-side tooling (see above).
- `docs/` -- this documentation set.

## Build & verify

```bash
cd Debug && make all -j4
```

then, since this project's `.cproject` does **not** have the "Convert to
binary file (.bin)" post-build step enabled (confirmed; unlike the
sibling project, which does), generate the `.bin` by hand:

```bash
arm-none-eabi-objcopy -O binary Debug/WHAM-PFMG474-V4.elf Debug/WHAM-PFMG474-V4.bin
```

**Adding a new source file from outside CubeIDE (e.g. this agent
writing a `.c`/`.h` pair directly)**: CubeIDE's managed build
auto-discovers new files in `Core/Src` the *first* time you build a
fresh project (no `Debug/` yet) -- but once `Debug/` exists, its
generated `Debug/Core/Src/subdir.mk` and `Debug/objects.list` are
**not** automatically refreshed by a plain `make`. Every file added
this way in this project (`boot_jump.c`, `cmd_parser.c`, `commands.c`,
`uart.c`) needed those two files hand-patched to add the new
`.c`/`.o`/`.d` entries before `make` would pick them up. In CubeIDE
itself, `F5` (Refresh) + a normal Build regenerates these correctly, no
hand-patching needed -- the manual patching is only necessary when
building from the command line without going through the IDE first.

- No on-host test suite. Verification so far = clean build (zero
  warnings) + real hardware round trips over the serial link (see
  `docs/changelog.txt` for what's actually been confirmed on hardware
  vs. only compiled/linked).
- Serial console: **9600 8N1**, SCPI-style mnemonics (case-insensitive,
  short/long form), `OK ...` / `ERR <code> <msg>` responses. Full
  protocol: `docs/command_reference.md`.

## Coding conventions

Same conventions as the sibling project (module-prefixed public
functions, `stdint.h` exact-width types, minimal non-blocking ISRs,
`volatile` on ISR-shared state, bugfix comments that state root cause +
symptom + why the fix works) -- there's no separate style doc here yet
since the codebase is still small; read `boot_jump.c` and `cmd_parser.c`
for the current standard to match.

## Hard-won invariants

1. **Baud rate is 9600 everywhere on purpose** -- `main.c`'s
   `huart2.Init.BaudRate = 9600` line lives in CubeMX-generated code
   *outside* any `USER CODE` marker. A `.ioc` "Generate Code" (the
   `.ioc` itself has no explicit baud parameter, since 9600 isn't
   CubeMX's HAL default) will silently reset it back to 115200 --
   re-apply 9600 if that ever happens. Same situation in the sibling
   project's `main.c`.
2. **`BootJump_CheckAndEnter()` must stay the literal first statement in
   `main()`**, before `HAL_Init()`. This is what lets the ROM bootloader
   jump-in path (entering, not the `Go` command discussed above) work
   reliably -- see `boot_jump.c`'s header comment.
3. **USART2's NVIC priority/enable call lives inside `MX_USART2_UART_Init()`'s
   `USER CODE BEGIN USART2_Init 2` block**, not in `USER CODE BEGIN 2`
   with everything else -- without it, `HAL_UART_Receive_IT()` arms but
   the interrupt never actually reaches the NVIC, and nothing over
   serial ever gets a reply. Easy to lose track of on a CubeMX regen if
   you're not looking in the right `USER CODE` block.

## Known gaps / in-flight work (as of 2026-08-31)

- **The VTOR/MEMRMP bugfix (see above) has not been ported to the
  sibling V3/V4 projects yet**, which carry the identical unfixed
  `boot_jump.c` this one was forked from. Deliberately deferred, not
  forgotten -- ask before assuming it should happen automatically.
- **PWM/HRTIM engine is ported (`hrtim.c`/`pfm.c`) but not wired into
  `main.c` or any command yet** -- see `docs/test_protocol.md`'s T3 for
  the developer-only way to exercise it today, and the known
  `PFM_Restart()`-applies-an-empty-table trap documented there (fix
  before wiring this into a real command, not after).
- **`HRTIM1` is not yet represented in the `.ioc`** -- `hrtim.c` fully
  self-configures the peripheral (including GPIO AF pin muxing, in its
  own `HAL_HRTIM_MspInit()`) without any CubeMX-generated code, so the
  firmware works regardless -- but CubeMX's Pinout view doesn't know
  PA8-11/PB12-13 are spoken for, and `HAL_HRTIM_MODULE_ENABLED`
  (`stm32g4xx_hal_conf.h`) is a hand-added line outside the `.ioc`'s
  own awareness (same regen-hazard category as the baud rate, above).
  In progress: configuring HRTIM1 properly through CubeMX's Pinout &
  Configuration tool for channels A/B/C (matching V3) plus D/E/F
  (reserved, unused) to close this gap -- see the backed-up
  `hrtim.c`/`hrtim.h`/`.ioc` this was based on before any GUI changes,
  since CubeMX regenerating this file from its own template would
  discard the hand-written implementation entirely (no `USER CODE`
  markers for it to preserve).
- **`_Min_Stack_Size` in `STM32G474QETX_FLASH.ld` is still the CubeMX
  nominal minimum (0x400 = 1 KB)**, not a value chosen for this
  firmware's actual worst-case call depth. Deliberately not changed
  yet (2026-08-31) -- revisit once `PFM_TABLE_SIZE` is actually raised
  from its current default (5000); the two decisions are linked (a
  larger table leaves less RAM headroom for a larger stack reservation
  to mean anything). Don't raise `PFM_TABLE_SIZE` far past 5000 without
  also revisiting this.
- **`HW_BOARD_REV` in `version.h` is a placeholder** (`"REVA"`) -- not
  yet confirmed against the actual board silkscreen.

## Documentation map

| Doc | Contents |
|---|---|
| `docs/command_reference.md` | Every serial command, SCPI matching rules, error codes |
| `docs/serial_reflash_guide.md` | How to use `wham_serial_flash.py`, the BOOT mechanism, the VTOR/MEMRMP fix, troubleshooting |
| `docs/changelog.txt` | Running change log, dated entries, root causes for fixes |
| `docs/test_protocol.md` | Numbered bench test protocol (T0-T3) -- markedly shorter than the sibling project's, since it only covers what actually exists here (serial identity, BOOT/reflash incl. the Go-jump regression check, and a developer-only HRTIM smoke test). Update it as functionality is added rather than assuming it tracks the sibling's group numbering. |
| `docs/sop/wham_pfmg474_v4_sop.tex` | Formal Standard Operating Procedure: build, bootstrap flash, serial reflash, verification, troubleshooting. Same LaTeX house style as the sibling project (`mathpazo`, 5in×8.25in `geometry`, plain `\hline` tables) -- compile with `pdflatex` (twice, for cross-references), then render to PNG and visually inspect before calling any edit done; this document's own `\tabcolsep` note explains a real overfull-table pitfall worth reading before adding another table. |
