/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// gameport.h - the NUKED SB-EMU Game Port (a stand-in device for PCs whose sound card has none)
// and the "NUKED SB-EMU Joystick" in Game Controllers, set up and removed without the Add New
// Hardware wizard. The device is written into the registry exactly as Windows 98 writes it when
// NUKEDJOY.INF is installed by hand (checked in a Windows 98 SE VM), so Device Manager shows it with
// its I/O address and Windows loads its joystick driver (VJoyD) for it after the next restart.
#pragma once
#include <string>

struct GamePortState {
    bool device = false;        // the NUKED game port is in Device Manager
    unsigned port = 0;          // its address (ForcedConfig), 0 = unknown
    bool type = false;          // the "NUKED SB-EMU Joystick" type is known to Game Controllers
    int joystick = 0;           // Windows joystick number (1..16) set to that type, 0 = none
    bool vxd = false;           // NUKEDJOY.VXD is in the SYSTEM folder
};

GamePortState gameport_state();

// copy NUKEDJOY.VXD/.INF from dir, register the joystick type, add the device at port and a
// joystick of that type (if none yet). log gets what was done. True = a restart is needed.
bool gameport_install(const std::string& dir, unsigned port, std::string& log);

// move the device to another address (Device Manager shows it after a restart). True = changed.
bool gameport_set_port(unsigned port);

// remove the device, the joystick type and the joystick (for the uninstaller and the settings window)
void gameport_remove(std::string& log);
