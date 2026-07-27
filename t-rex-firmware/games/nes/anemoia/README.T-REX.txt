Anemoia-ESP32 — vendored NES emulator core
==========================================

This directory contains the Anemoia-ESP32 NES emulator core, vendored into the
T-REX firmware tree (compiled as project sources; see NOTICES #20 for why it is
in-tree rather than under lib/).

  Upstream:  https://github.com/Shim06/Anemoia-ESP32
  Author:    Shim06
  License:   GNU General Public License v3.0 (GPL-3.0) — full text in ./LICENSE

The code here is Shim06's work under GPL-3.0. T-REX's own integration layer
(games/nes/nes_emulator.cpp/.h) is a separate file and is not part of this core.

Local patches applied for the T-Deck port are listed in the repository NOTICES
file (entry #20): TFT_eSPI replaced with a display-flush callback, a debug.h
build-flag #error removed, an include-path fix, T-Deck I2S pin constants, and
bus.cpp save/load-state path moved from /states to /nes/states (grouped under
the emulator's SD folder). All other core files (CPU, PPU, APU, mappers
0-4+069) are upstream verbatim.

Because this core is GPL-3.0 and is distributed as part of T-REX, the combined
work is distributed under the GNU AGPL-3.0-or-later (GPLv3 and AGPLv3 are
mutually compatible). The GPL-3.0 terms in ./LICENSE continue to apply to the
files in this directory.
