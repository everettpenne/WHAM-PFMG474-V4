# WHAM-PFMG474-V4 — Test Protocol

Revision: 2026-08-31. Covers firmware through: the serial command
architecture (SCPI-style dispatch), `*IDN?`, `BOOT`/serial reflash
(including the `SCB->VTOR`/`SYSCFG->MEMRMP` Go-jump bugfix), and the
ported-but-unwired HRTIM/PFM PWM engine. See `docs/changelog.txt` for
the dated history behind each of these.

Conventions: every serial command ends CRLF at **9600 8N1**; responses
are `OK …` or `ERR <n> <msg>`. Tests are numbered `T<group>.<n>`; run
groups in order the first time. This protocol is deliberately much
shorter than the sibling PFM-STM32G474 project's — **this firmware has
no state machine, no ARM/FIRE, and no command that reaches the PWM
engine yet.** Don't extrapolate test groups from that project onto this
one; the groups below are only the ones that actually apply.

**Safety:** T0–T2 involve no code path that reaches HRTIM at all — the
board cannot drive PA8/9/10/11 or PB12/13 during those tests regardless
of what's connected to them. **T3 is different**: it requires a
temporary firmware change to exercise the (currently unwired) PWM
engine directly, and — unlike T0–T2 — it drives real voltage
transitions on those six pins the moment it runs. Treat any gate driver
or power stage wiring on those pins as live for the duration of T3, per
your lab's normal HV/gate-driver procedure, even though this firmware
has no closed-loop or fault protection behind it yet.

---

## T0 — Build & flash sanity

| # | Step | Expect |
|---|---|---|
| T0.1 | Build in CubeIDE (Debug config), or `cd Debug && make all -j4` | No errors, no warnings. |
| T0.2 | `arm-none-eabi-objcopy -O binary Debug/WHAM-PFMG474-V4.elf Debug/WHAM-PFMG474-V4.bin` | Produces a `.bin` — this project's build config does not do this automatically (see `docs/serial_reflash_guide.md`). |
| T0.3 | Check `.map` for `g_pfmTable` | **Absent** on a normal build — nothing calls into `pfm.c` yet, so `--gc-sections` strips it entirely. Its presence (at 40000 B = 5000 × 8-byte `PFM_Step_t`) is only meaningful during T3's temporary wiring; its *absence* here is the correct, expected result, not a build problem. |
| T0.4 | Flash (ST-Link via CubeIDE, or `python/wham_serial_flash.py` once a `BOOT`-capable build is already on the chip — see `docs/sop/wham_pfmg474_v4_sop.tex`) | Board boots; no spontaneous serial output. |

## T1 — Serial link & identity

| # | Step | Expect |
|---|---|---|
| T1.1 | `*IDN?` | `OK WHAM-PFMG474-V4 <rev> <version>` (fields from `version.h`). |
| T1.2 | `BOGUS` | `ERR 1 Unknown command`. |
| T1.3 | `*idn?` (lowercase) | Same reply as T1.1 — dispatch is case-insensitive (see `docs/command_reference.md`). |
| T1.4 | `IDN?` (missing the leading `*`) | `ERR 1` — `*` is part of the mandatory short form for this common command, not optional. |

## T2 — Serial bootloader / reflash

Prereq: a `BOOT`-capable build already on the chip (see T0.4 / the SOP's
bootstrap-flash procedure if starting from a board that predates
`boot_jump.c`).

| # | Step | Expect |
|---|---|---|
| T2.1 | Send `BOOT` directly (any terminal) while the board is idle | `OK ENTERING BOOTLOADER`, then the link drops as the MCU resets into the ROM bootloader. |
| T2.2 | `stm32flash <port>` (info query only, no `-w`) against the now-bootloader-resident chip | Reports Device ID `0x0469` (STM32G47xxx/48xxx) — confirms the physical link and bootloader entry independent of any application firmware. |
| T2.3 | Power-cycle to recover, then run `python3 python/wham_serial_flash.py --port <port>` end-to-end with a version-bumped build | Script reports `BOOT` ACK'd, then `[done] firmware written and verified.` |
| T2.4 | **Immediately** after T2.3 (no manual reset), query `*IDN?` | Must return the **new** version on the first query. **Getting no reply here — needing a manual reset to recover — is the Go-jump/VTOR regression** (see `docs/changelog.txt`, 2026-08-31 bugfix entry, and `boot_jump.c`'s `BootJump_CheckAndEnter()`). Report it, don't work around it. |
| T2.5 | Repeat T2.3–T2.4 once more, back to back | Same clean result both times — this was flaky-looking before the fix (worked once, needed a reset the next), so a single pass isn't sufficient evidence. |
| T2.6 | `BOOT` while a build has `BOOT_JUMP_FEATURE_ENABLED` set to `0` (`boot_jump.h`) | `ERR 1 Unknown command` — confirms the module's removability actually removes the command, not just disables its effect. |

## T3 — PWM/HRTIM engine (developer smoke test, not an operator test)

No serial command reaches this code today — `HRTIM1_FullInit()`,
`PFM_Init()`, etc. are not called from `main()` (see `AGENTS.md`). This
section exercises them directly by temporarily editing `main.c`, the
same way this was verified during development; it is not something a
non-developer should run, and the edit must be reverted afterward.

**Known gap, read before running T3.2**: `PFM_Restart()` applies
`g_pfmTable[0]` before starting the counters — and with no table
builder ported (see `pfm.h`), that entry is always the zero-initialized
default (`per=0, cmpA=cmpB=cmpC=0`), not `HRTIM1_FullInit()`'s own
~100 kHz/50 %-duty init defaults. Nothing calls
`PFM_CycleBoundaryHandler()` on a timer yet either (no HRTIM master
interrupt is wired up), so a real call to `PFM_Restart()` today would
leave the counters running at `PER≈0` indefinitely, not the sensible
default waveform. **T3.2 below tests `hrtim.c` directly, bypassing
`pfm.c`, specifically to avoid this trap** — do not substitute
`PFM_Restart()` for `HRTIM1_PWM_Start()` in T3.2 expecting the same
result.

| # | Step | Expect |
|---|---|---|
| T3.1 | In `main.c`, add `#include "hrtim.h"` and, in `USER CODE BEGIN 2` (after `uart_init(...)`), add `HRTIM1_FullInit(); HRTIM1_PWM_Start(1, 1, 1);` | Builds clean. `.map` now shows real HRTIM code linked in (text size grows by several KB vs. a normal build — see T0.3's note on why it's normally absent). |
| T3.2 | Flash T3.1's build. Scope TA1/TA2 (PA8/PA9), TB1/TB2 (PA10/PA11), TC1/TC2 (PB12/PB13) | All three pairs: ~100 kHz complementary square wave, ~50 % duty, ~100 ns dead time between an edge on one output and the corresponding edge on its complement. |
| T3.3 | Scope U (PA8) vs. V (PA10) vs. W (PB12) rising edges relative to each other | 120° / 240° spacing (≈3.33 µs / 6.67 µs apart at 100 kHz) — confirms the Master `PER`/`CMP1`/`CMP2` phase-trigger scheme, not just that each pair individually toggles. |
| T3.4 | Revert `main.c` to the pre-T3.1 state; rebuild | Back to T0.1/T0.3's normal result (no HRTIM code linked, `g_pfmTable` absent). **Do not leave T3.1's edit in place** — it was a one-off verification, not a real integration. |

---

## Regression quick-list (run after any firmware change)

1. T1.1 (identity)
2. T2.3–T2.5 (reflash round trip + the Go-jump regression check specifically)
3. T3.1–T3.3 only if `hrtim.c`/`pfm.c` changed — otherwise this whole
   group can be skipped, since nothing else in the firmware depends on
   them yet.
