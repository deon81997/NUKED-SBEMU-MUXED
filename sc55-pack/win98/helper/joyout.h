// joyout.h - what the pad becomes besides keys: the stick type of the DOS game port
// (CH Flightstick Pro / Thrustmaster FCS / Gravis GrIP, as SBEMU's /JOYMODE) and the
// NUKED joysticks Windows programs see (NUKEDJOY.VXD, Game Controllers type
// "NUKED SB-EMU Joystick").
#pragma once
#include "pad.h"
#include "../common/sc55wif.h"
#include <string>

struct JoyOutSettings {
    int type = 0;            // game port stick type: 0 standard, 1 CH, 2 FCS, 3 GrIP
    int nuked = 1;           // feed the NUKED joysticks
    int mode = 0;            // NUKED joystick: 0 = pad (6 axes, hat, 16 buttons), 1 = the game port as DOS sees it
    int deadzone = 10;       // percent, 0..50
};

void joyout_load(const std::string& ini, JoyOutSettings& s);
void joyout_save(const std::string& ini, const JoyOutSettings& s);

// shape the DOS game port for the stick type; GrIP frames go to sh
void joyout_port(const JoyOutSettings& s, const PadInput in[2], PadOutput& out, SC55W_SHARED* sh);

// fill sh->wjoy[] from the pads (in[k].present = pad k there)
void joyout_windows(const JoyOutSettings& s, const PadInput in[2], const PadOutput& port, SC55W_SHARED* sh);

// GrIP frame of a pad (24 bits, bit 0 first), 0 = no pad
uint32_t joyout_grip_frame(const PadInput& in);

// a NUKED joystick that Game Controllers added has no calibration (VJoyD then gives full scale):
// write the NUKED values (0..65535, centre 32768, all calibrated) into its Joystick<n>Configuration.
// Returns true when something was written (then joyConfigChanged has been called).
bool joyout_fix_calibration(unsigned joyid);
