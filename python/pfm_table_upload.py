#!/usr/bin/env python3
"""
pfm_table_upload.py -- build a PFM table on the host and upload it to
WHAM-PFMG474-V4 over the TABle:BEGin / TABle:STEP / TABle:END serial
commands (see docs/command_reference.md).

Table CONSTRUCTION deliberately lives entirely in this script, not in
firmware -- see pfm.h's "ADDED" header note and Core/Src/commands.c's
cmd_table_step() for why. The firmware side only knows how to accept
and store whatever (per, cmpA, cmpB, cmpC) tuples it's sent; it does no
frequency/duty math and has no idea what "profile" produced them.

This first version is intentionally narrow: a small set of fixed
profiles, selected by editing PROFILE below before running -- no CLI
flag, no general-purpose profile language. All five share the same
100 kHz carrier and the same ~5 ms total shot duration (500 periods),
so they're directly comparable on a scope. What "profile" ends up
meaning long-term (a real DSL? more shapes? per-phase-independent
duty?) is explicitly undecided -- don't read more structure into this
than "fixed test cases," and expect this file to be rewritten, not
incrementally extended forever, once that's figured out.

Usage:
  1. Edit PROFILE below (1-5).
  2. python3 python/pfm_table_upload.py --port /dev/cu.usbserial-XXXXX
"""

import argparse
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("error: pyserial not installed. Run:  pip install pyserial")

# --- Profile selection -----------------------------------------------------
# Edit this, then run the script. No other configuration is read from
# the command line for the profile itself.
PROFILE = 3
#   1 = constant 100 kHz, constant 50% duty, all 3 phases
#   2 = constant 100 kHz, duty ramps 25% -> 75%, all 3 phases
#   3 = constant 100 kHz, duty ramps 90% -> 10%, all 3 phases
#   4 = constant 100 kHz, duty ramps 10% -> 90%, all 3 phases
#   5 = constant 100 kHz, duty ramps 90% -> 25%, all 3 phases

# --- Fixed parameters, shared by all five profiles --------------------------

APP_BAUD = 115200  # raised from 9600 on 2026-09-04 -- see AGENTS.md and
                    # docs/changelog.txt. WHAM-PFMG474-V4-only; the
                    # sibling PFM-STM32G474 project still uses 9600.

# Must match hrtim.h's HRTIM_TIMER_CLK_HZ exactly -- this is the real,
# hardware-verified HRTIM kernel clock (see main.c's SystemClock_Config()),
# not something this script can discover on its own.
HRTIM_TIMER_CLK_HZ = 170_000_000

FREQ_HZ = 100_000  # carrier frequency, all five profiles

# One PWM period = one table entry, held for one period each (matches
# firmware's PFM_HOLD_PERIODS = 1 -- see pfm.h). 500 periods at 100 kHz
# = 5 ms, matching the sibling PFM-STM32G474 project's own 5 ms default
# pulse-length convention, so results are easy to compare against it.
TOTAL_PERIODS = 500

# For all four ramp profiles: number of DISTINCT duty levels the ramp
# is broken into, each held for several periods (a staircase
# approximating a linear ramp, matching the sibling project's
# SWEEP-segment convention of "N steps x M dwell periods"). 50 x 10 =
# 500, matching TOTAL_PERIODS above.
RAMP_STEPS = 50
RAMP_DWELL_PERIODS = TOTAL_PERIODS // RAMP_STEPS  # 10


def per_from_freq(freq_hz):
    """PWM period register value for a given carrier frequency -- same
    formula as hrtim.c's own hardcoded init default (Period=1699 for
    100 kHz at 170 MHz): round(clock / freq) - 1."""
    return round(HRTIM_TIMER_CLK_HZ / freq_hz) - 1


def cmp_from_duty(per, duty_pct):
    """Compare register value for a given duty percentage at this
    period. Output is ACTIVE from the period-start (PER) event until
    CMP1 (see hrtim.c's pOutputCfg: SetSource=TIMPER, ResetSource=
    TIMCMP1), so duty fraction = CMP1/PER, same relationship
    HRTIM1_FullInit()'s own init default already uses (850/1699 =~
    50.0% at PER=1699). Clamped to [2, per-2] -- matching hrtim.c's
    HRTIM1_ClampCompare() margin -- so this script never relies on
    that firmware-side clamp as anything but a backstop, per pfm.h's
    documented philosophy (validate before, don't rely on it after)."""
    cmp_val = round(per * (duty_pct / 100.0))
    return max(2, min(per - 2, cmp_val))


def build_table():
    """Returns a list of (per, cmpA, cmpB, cmpC) tuples for the
    selected PROFILE. All 3 phases always get the same duty -- none of
    the five profiles differ per-phase."""
    per = per_from_freq(FREQ_HZ)
    steps = []

    if PROFILE == 1:
        cmp_val = cmp_from_duty(per, 50.0)
        steps = [(per, cmp_val, cmp_val, cmp_val)] * TOTAL_PERIODS

    elif PROFILE == 2:
        steps = _ramp(per, 25.0, 75.0)

    elif PROFILE == 3:
        steps = _ramp(per, 90.0, 10.0)

    elif PROFILE == 4:
        steps = _ramp(per, 10.0, 90.0)

    elif PROFILE == 5:
        steps = _ramp(per, 90.0, 25.0)

    else:
        sys.exit(f"error: PROFILE must be 1-5 (got {PROFILE!r})")

    return steps


def _ramp(per, duty_start_pct, duty_end_pct):
    steps = []
    for i in range(RAMP_STEPS):
        # Duty at this level -- linear interpolation across RAMP_STEPS
        # levels, inclusive of both endpoints (level 0 = duty_start,
        # level RAMP_STEPS-1 = duty_end).
        frac = i / (RAMP_STEPS - 1)
        duty_pct = duty_start_pct + frac * (duty_end_pct - duty_start_pct)
        cmp_val = cmp_from_duty(per, duty_pct)
        steps.extend([(per, cmp_val, cmp_val, cmp_val)] * RAMP_DWELL_PERIODS)
    return steps


def send_and_expect_ok(ser, line, timeout=2.0):
    """Send one command line, read back a reply, return it. Raises on
    ERR or a missing/garbled reply -- this script does not try to
    recover mid-upload, it just stops and reports where it got to.

    Uses read_until() rather than a manual read(128)-in-a-loop poll.
    pyserial's read(size) keeps trying to fill the *entire* requested
    size until the port's own timeout elapses, even once a short reply
    (our replies are always just a few bytes, e.g. "OK\\r\\n") has
    already fully arrived -- so with the port opened at timeout=1.0,
    every single round-trip was costing close to a full second
    regardless of the real ~20-30 ms wire time at 9600 baud. 500
    TABLE:STEP entries were taking 8+ minutes because of this read
    pattern, not because of the baud rate itself -- read_until(b"\\n")
    returns the instant the terminator shows up, only falling back to
    the port's configured timeout if a reply never arrives at all
    (a real failure, which still needs to time out and raise below)."""
    ser.reset_input_buffer()
    ser.write((line + "\r\n").encode("ascii"))
    ser.flush()

    ser.timeout = timeout
    reply = ser.read_until(b"\n")
    reply = reply.decode("ascii", errors="replace").strip()

    if not reply.startswith("OK"):
        raise RuntimeError(f"sent {line!r}, got {reply!r}")
    return reply


def main():
    ap = argparse.ArgumentParser(description="Upload a PFM table to WHAM-PFMG474-V4")
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/cu.usbserial-XXXXX")
    ap.add_argument("--baud", type=int, default=APP_BAUD, help=f"default: {APP_BAUD}")
    args = ap.parse_args()

    table = build_table()
    per = table[0][0]
    print(f"=== PFM table upload: profile {PROFILE} ===")
    print(f"    entries : {len(table)}")
    print(f"    per     : {per}  (={HRTIM_TIMER_CLK_HZ / (per + 1):.1f} Hz carrier)")
    print(f"    port    : {args.port} @ {args.baud} 8N1")
    print()

    try:
        with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
            time.sleep(0.2)  # let the port settle before the first command

            send_and_expect_ok(ser, "TABLE:BEGIN")
            print("[upload] TABLE:BEGIN ok, uploading...")

            for i, (per, cmp_a, cmp_b, cmp_c) in enumerate(table):
                send_and_expect_ok(ser, f"TABLE:STEP {per} {cmp_a} {cmp_b} {cmp_c}")
                if (i + 1) % 50 == 0 or (i + 1) == len(table):
                    print(f"[upload] {i + 1}/{len(table)}")

            reply = send_and_expect_ok(ser, "TABLE:END")
            print(f"[upload] TABLE:END -> {reply}")

    except (serial.SerialException, RuntimeError) as exc:
        sys.exit(f"\n[fail] {exc}")

    print("\n[done] table uploaded and confirmed.")


if __name__ == "__main__":
    main()
