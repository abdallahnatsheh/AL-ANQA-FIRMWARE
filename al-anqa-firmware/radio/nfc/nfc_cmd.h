/**
 * @file   nfc_cmd.h
 * @brief  Entry point for the `nfc` / `nm` command tree (T-Pager).
 *
 * runNfc(args) dispatches subcommands. On boards without BOARD_HAS_NFC it
 * prints a "not on this board" line and returns — so command_manager can
 * register `nfc` unconditionally without pulling in the RFAL stack.
 *
 * Phase 0 / Slice 1: only `info` + `help` (chip-ID probe over raw SPI, no
 * RFAL). Later slices add scan/dump/emu/… per docs/plans/nfc-ultimate-tpager.md.
 */
#pragma once

void runNfc(char* args);
