/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// install98.h - SC55W.EXE /INSTALL and /UNINSTALL, and the BLASTER line
#pragma once
#include <windows.h>
#include <string>
#include "opts.h"

// BLASTER= value for these options, e.g. "A220 I5 D1 H5 P330 T6"
std::string blaster_for(const Options& o);
// If SETUP put a BLASTER line in AUTOEXEC.BAT, bring it up to date.
// Returns true if the file was changed (new DOS boxes need a restart of Windows).
bool autoexec_update_blaster(const Options& o);
// Ask a running SC55W.EXE to quit and wait for it (up to 10 s).
void stop_running_helper();

int run_install(HINSTANCE inst);
int run_uninstall(HINSTANCE inst);
