/*
 * version.h
 *
 * The project's one compile-time config file -- board/firmware identity
 * (reported by *IDN?, commands.c) plus project-wide constants like
 * PWM_NUM_CHANNELS below. Kept in one place on purpose (project
 * decision, 2026-08-31): the sibling PFM-STM32G474 project has a single
 * config file (supply_config.h) for exactly this reason rather than one
 * file per peripheral/module, and this project follows the same
 * convention. Add new project-wide constants here rather than starting
 * a new config header per module.
 *
 * (Module-local, self-contained flags are a deliberate exception --
 * e.g. BOOT_JUMP_FEATURE_ENABLED lives in boot_jump.h itself, because
 * that module's whole design point is being independently removable in
 * one place. Don't move flags like that here; this file is for things
 * that describe the *project*, not one module's own on/off switch.)
 */

#ifndef INC_VERSION_H_
#define INC_VERSION_H_

/* Board/product identity. HW_BOARD_REV is a placeholder -- set it to
 * match this build's actual PCB silkscreen revision and bump it on
 * every hardware respin so *IDN's report always matches what's
 * physically in hand. */
#define HW_BOARD_NAME     "WHAM-PFMG474-V4"
#define HW_BOARD_REV      "REVA"

/* Firmware identity. Bump on every release the way the sibling
 * PFM-STM32G474 project does (FW_VERSION_STRING in its
 * supply_config.h). */
#define FW_VERSION_STRING "v0.6"

/* --------------------------------------------------------------------------
 * PWM / HRTIM channel count
 *
 * docs/pin_mapping_v4.csv wires out all 6 HRTIM1 channel pairs
 * (A-F / U,V,W,X,Y,Z) on this board, vs. only 3 (A/B/C, U/V/W) on the
 * sibling PFM-STM32G474 project's hardware. hrtim.c/pfm.c here are
 * ported from that 3-channel sibling design UNCHANGED -- hardcoded to
 * Timers A/B/C, exactly reproducing its behavior (per project
 * decision, 2026-08-31: avoid the complexity of a fully general
 * N-channel HRTIM init for now; add it later if/when it's needed).
 *
 * PWM_NUM_CHANNELS documents that decision -- it is NOT read by
 * hrtim.c today; changing this value alone does nothing. Treat it as
 * the marker for "this is where channel-count configurability would
 * be wired in" rather than a working switch. Also worth noting before
 * generalizing past 3: the HRTIM Master timer has exactly 4 compare
 * units (MCMP1R-MCMP4R), giving at most 5 Master-driven, evenly-spaced
 * phase trigger points (PER + 4 CMPs) -- enough for channels A-E, but
 * NOT a 6th independently-phased channel without a different
 * triggering scheme (e.g. one slave timer resetting off another
 * slave's event rather than off Master directly). Confirm that before
 * assuming PWM_NUM_CHANNELS can simply become 6.
 * -------------------------------------------------------------------------- */
#define PWM_NUM_CHANNELS  (3U)

#endif /* INC_VERSION_H_ */
