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

## Current functionality (as of 2026-09-04)

3-phase PWM generation (HRTIM1, Timers A/B/C active, D/E/F reserved)
plus the serial command layer. A PFM table can be uploaded, its length
confirmed, and (as of 2026-09-04) fired: `FIRE` starts playback from
step 0 via the HRTIM master-repetition interrupt, and it auto-stops
when the table is exhausted. There is still **no state
machine** -- no ARM precondition, no interlock, no fault gating; `FIRE`
takes effect immediately whenever sent, per current project decision
(see `docs/command_reference.md`'s `FIRE` entry).

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
  - `pfm_table_upload.py` -- builds one of 5 fixed PFM shot profiles
    (see below) and uploads it via `TABLE:*`.
- **PWM/HRTIM engine** (`hrtim.c`, `pfm.c`) -- ported from the sibling
  project and wired into `main.c` (see `docs/changelog.txt`, 2026-09-04
  entries, for the full CubeMX-vs-hand-code saga this took to get
  right -- read that before touching `hrtim.c`'s `HAL_HRTIM_MspInit()`
  or the `.ioc`). Timers A/B/C are the sibling project's exact 3-phase
  setup (120°/240° Master-synced, 100 ns dead time); D/E/F are
  initialized identically but NOT phase-locked to Master (only 4
  Master compare units exist -- enough for 5 synced phases, not 6) and
  NOT started by `HRTIM1_PWM_Start()`, which still only takes U/V/W.
  `PWM_NUM_CHANNELS` in `version.h` documents this, it doesn't control it.
- **`TABLE:BEGIN`/`STEP`/`END`/`?`** (`commands.c`, primitives in
  `pfm.c`) -- uploads a complete PFM table (`PFM_Step_t` entries) built
  entirely off-controller. Firmware does zero construction/validation
  beyond "does it fit" -- see `pfm.h`'s "ADDED" header note.
  `python/pfm_table_upload.py` is the reference builder: 5 fixed
  profiles today (constant 50% duty; 25%->75% ramp; 90%->10% ramp;
  10%->90% ramp; 90%->25% ramp), selected by editing a constant before
  running.
  `FIRE` plays an uploaded table back -- see "Current functionality"
  above.

## Tech stack & layout

- C11, STM32Cube HAL (G4), bare metal -- no RTOS, no heap.
- Built with STM32CubeIDE's generated makefile; toolchain
  arm-none-eabi-gcc 13.3.
- `Core/Src|Inc/` -- all project code. Currently: `main.c`, `uart.c`,
  `cmd_parser.c`, `commands.c`, `boot_jump.c`, `hrtim.c`, `pfm.c`, plus
  CubeMX-generated `stm32g4xx_hal_msp.c`/`stm32g4xx_it.c`/
  `system_stm32g4xx.c`/`syscalls.c`/`sysmem.c`.
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
- Serial console: **115200 8N1** (raised from 9600 on 2026-09-04, see
  "Hard-won invariants" below and `docs/changelog.txt`), SCPI-style
  mnemonics (case-insensitive, short/long form), `OK ...` /
  `ERR <code> <msg>` responses. Full protocol: `docs/command_reference.md`.

## Coding conventions

Same conventions as the sibling project (module-prefixed public
functions, `stdint.h` exact-width types, minimal non-blocking ISRs,
`volatile` on ISR-shared state, bugfix comments that state root cause +
symptom + why the fix works) -- there's no separate style doc here yet
since the codebase is still small; read `boot_jump.c` and `cmd_parser.c`
for the current standard to match.

## Hard-won invariants

1. **Baud rate is 115200 everywhere on purpose** (raised from 9600 on
   2026-09-04, real-hardware experiment -- see `docs/changelog.txt`;
   `python/wham_serial_flash.py`, `python/pfm_table_upload.py`, and
   `python/scpi.py` were all updated to match). `main.c`'s
   `huart2.Init.BaudRate = 115200` line lives in CubeMX-generated code
   *outside* any `USER CODE` marker. A `.ioc` "Generate Code" would
   reset it back to HAL's own default -- which, as it happens, IS
   115200 right now, so a regen wouldn't silently break this today.
   That's a coincidence, not a guarantee: if this value ever needs to
   change again, re-apply it explicitly rather than trusting a regen to
   land on the right number by chance. This is a WHAM-PFMG474-V4-only
   divergence -- the sibling PFM-STM32G474 project still uses 9600, and
   nothing here implies porting this change there.
   - **921600 does not work on this hardware** -- tried directly after
     115200 (skipping the standard steps in between) as part of the
     same experiment: consistently garbled, same-length-but-wrong-content
     replies (a real baud mismatch/reliability failure, not a fluke --
     retried 3x). Left unexplored which of 230400/460800 is the actual
     practical ceiling; 115200 was chosen as "clearly safe and already a
     large win," not as "the fastest verified-working rate."
   - **If a bad baud change ever strands the board** (as 921600 did,
     mid-experiment): the serial `BOOT` command needs a working link to
     even request the ROM bootloader, so it can't recover a board stuck
     at a wrong baud. An ST-Link + `st-flash write <bin> 0x08000000`
     (or `st-info --probe` first to confirm it's detected) recovers
     over SWD, completely bypassing the broken serial link -- confirmed
     working for exactly this scenario the same day.
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
- **RESOLVED 2026-09-04: `FIRE` now exists.** `HRTIM1_EnableMasterInterrupt()`
  (`hrtim.c`) and `HRTIM1_Master_IRQHandler()` (`stm32g4xx_it.c`) were
  ported from the sibling project (state-machine/fault-pin calls
  stripped, since neither exists here), enabled once at boot from
  `main.c` (after `FixSysTickPriority()`, also newly ported, has raised
  SysTick off the HAL default -- see that function's own doc comment
  for the priority scheme: SysTick=0, HRTIM1_Master=1, USART2=2).
  `cmd_fire()` (`commands.c`) wraps `PFM_Restart()`, rejecting with
  `ERR 5` against an empty table. No ARM/state-machine interlock was
  added -- `FIRE` always takes effect immediately; adding an interlock
  is future work, not implied by this change. See
  `docs/test_protocol.md`'s T3 for the (now largely superseded, but
  still useful for `hrtim.c`-only bring-up) developer-only direct-call
  path this bypassed. **Verified end-to-end on real hardware the same
  day** (flash -> upload a real 500-entry table -> `FIRE` -> `OK`) --
  command-layer round trip only, no scope available in that pass, so
  the actual waveform (T4.5-T4.8) is still unobserved. That same pass
  also caught and fixed an unrelated ~25-50x slowdown in
  `pfm_table_upload.py`'s own reply-reading loop -- see
  `docs/changelog.txt`, not a firmware issue.
- **RESOLVED 2026-09-04: cold-start phase-lock glitch on every `FIRE`,
  fully confirmed on real hardware.** The waveform T4.5-T4.8 left
  unobserved above WAS observed, via DSLogic captures -- and went
  through three iterations before it was actually right (full story in
  `docs/changelog.txt`'s several 2026-09-04 entries; this is the final
  state only). Root cause: `HRTIM1_PWM_Start()`'s force-ACTIVE step
  (needed to avoid an undefined complementary-pair state, per ST's own
  guidance) puts U/V/W's main outputs HIGH at the same instant with no
  regard for their intended 120°/240° stagger, and Timers B/C
  (`ResetTrigger = MASTER_CMP1`/`MASTER_CMP2`) don't become genuinely
  phase-locked until they've received that first reset -- which, unlike
  Timer A's (`MASTER_PER`, coincides with its own natural rollover), an
  external reset does NOT itself regenerate the output's SET state.
  Fixed in `HRTIM1_PWM_Start()`: each phase now waits for and connects
  to its own pins SEPARATELY, at its own reset-trigger event (V at
  `MASTER_CMP1`, W at `MASTER_CMP2`, U at `MASTER_PER`/`MREP`, via the
  new static helper `HRTIM1_WaitForPhaseAndConnect()`), with V/W's
  output re-forced ACTIVE at that exact moment (a direct `SETx1R`/
  `RSTx2R` write, same reasoning as the direct `OENR` write) so their
  first visible pulse is a genuine fresh start, not a snapshot of
  whatever their comparator had been doing since an earlier, still-
  hidden reset. Global interrupts are masked for the whole sequence
  (`HRTIM_MASTER_IT_MREP`/`MCMP1`/`MCMP2`, all armed at boot by
  `HRTIM1_EnableMasterInterrupt()`, would otherwise fire mid-sequence
  and let `PFM_CycleBoundaryHandler()` silently advance the table
  before step 0 was ever visible) -- bounded by a spin-count ceiling,
  not `HAL_GetTick()`, since that can't advance while masked.

  **Confirmed on real hardware**: a ~5 ms DSLogic capture spanning 499
  of profile 3's 500 periods shows phase spacing holding at
  ~3300 ns/~6600-6700 ns (target 3333/6667 ns) consistently from the
  cold-start edge through the last period, and U's duty tracking the
  90% -> 10% ramp cleanly throughout, landing exactly on 10.0% at the
  final period -- the fix holds for the entire shot, not just the
  startup instant.
- **`HRTIM1` *is* represented in the `.ioc` now** (as of 2026-09-04,
  hand-constructed, not GUI-generated -- see `docs/changelog.txt`).
  Do NOT reopen HRTIM1's Mode/Configuration panel in CubeIDE's Pinout &
  Configuration GUI -- doing so once already triggered a full
  regeneration that silently rewrote the clock tree and produced
  duplicate-symbol link errors against `hrtim.c`'s hand-written
  `HAL_HRTIM_MspInit()`/`hhrtim1`. Other peripherals' pinout remains
  safe to configure through the GUI normally.
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
