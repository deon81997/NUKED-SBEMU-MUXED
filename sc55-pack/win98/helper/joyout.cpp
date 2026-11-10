// joyout.cpp - see joyout.h
#include "joyout.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

void joyout_load(const std::string& ini, JoyOutSettings& s)
{
    const char* f = ini.c_str();
    s.type = GetPrivateProfileIntA("Joystick", "Type", 0, f);
    if (s.type < 0 || s.type > 3) s.type = 0;
    s.nuked = GetPrivateProfileIntA("Joystick", "NukedJoystick", 1, f) != 0;
    s.mode = GetPrivateProfileIntA("Joystick", "NukedMode", 0, f) ? 1 : 0;
    s.deadzone = GetPrivateProfileIntA("Joystick", "DeadZone", 10, f);
    if (s.deadzone < 0) s.deadzone = 0;
    if (s.deadzone > 50) s.deadzone = 50;
}

void joyout_save(const std::string& ini, const JoyOutSettings& s)
{
    char b[16];
    const char* f = ini.c_str();
    snprintf(b, sizeof b, "%d", s.type);      WritePrivateProfileStringA("Joystick", "Type", b, f);
    snprintf(b, sizeof b, "%d", s.nuked);     WritePrivateProfileStringA("Joystick", "NukedJoystick", b, f);
    snprintf(b, sizeof b, "%d", s.mode);      WritePrivateProfileStringA("Joystick", "NukedMode", b, f);
    snprintf(b, sizeof b, "%d", s.deadzone);  WritePrivateProfileStringA("Joystick", "DeadZone", b, f);
}

uint32_t joyout_grip_frame(const PadInput& in)
{
    if (!in.present) return 0;
    const bool* b = in.button;
    uint32_t f = 0x7C0000UL;                       // bits 18-22: sync
    int x = b[PB_DLEFT] ? -1 : b[PB_DRIGHT] ? 1 : in.axis[PA_LX] < -16384 ? -1 : in.axis[PA_LX] > 16384 ? 1 : 0;
    int y = b[PB_DUP] ? -1 : b[PB_DDOWN] ? 1 : in.axis[PA_LY] < -16384 ? -1 : in.axis[PA_LY] > 16384 ? 1 : 0;
    if (b[PB_START]) f |= 1UL << 0;
    if (b[PB_BACK])  f |= 1UL << 1;
    if (b[PB_RT])    f |= 1UL << 2;
    if (b[PB_Y])     f |= 1UL << 3;
    if (b[PB_LT])    f |= 1UL << 5;
    if (b[PB_A])     f |= 1UL << 6;
    if (b[PB_B])     f |= 1UL << 7;
    if (b[PB_X])     f |= 1UL << 8;
    if (b[PB_LB])    f |= 1UL << 10;
    if (b[PB_RB])    f |= 1UL << 11;
    if (y < 0) f |= 1UL << 12;
    if (y > 0) f |= 1UL << 13;
    if (x > 0) f |= 1UL << 15;
    if (x < 0) f |= 1UL << 16;
    return f;
}

void joyout_port(const JoyOutSettings& s, const PadInput in[2], PadOutput& out, SC55W_SHARED* sh)
{
    const PadInput& p = in[0];
    if (sh) sh->joy_mode = (u32)s.type;
    switch (s.type) {
    case 1:                                        // CH Flightstick Pro: hat pulls several lines
        if (p.present) {
            const bool* b = p.button;
            uint8_t lines = b[PB_DUP] ? 0xF : b[PB_DRIGHT] ? 0xB : b[PB_DDOWN] ? 0x7 : b[PB_DLEFT] ? 0x3 : 0;
            for (int i = 0; i < 4 && !lines; i++) if (out.joy_buttons >> i & 1) lines = (uint8_t)(1 << i);
            out.joy_buttons = lines;
        }
        break;
    case 2:                                        // Thrustmaster FCS: hat on axis 4
        if (p.present) {
            const bool* b = p.button;
            out.joy_axis[3] = b[PB_DUP] ? 0 : b[PB_DRIGHT] ? 16384 : b[PB_DDOWN] ? 32768 : b[PB_DLEFT] ? 49152 : 65535;
            out.joy_present |= 2;
        }
        break;
    case 3:                                        // GrIP: the VxD clocks the frames out
        if (sh) { sh->joy_grip[0] = joyout_grip_frame(in[0]); sh->joy_grip[1] = joyout_grip_frame(in[1]); }
        break;
    }
}

static uint16_t dz_axis(int v, int dz)          // -32768..32767 -> 0..65535 with a dead zone
{
    int lim = 32767 * dz / 100;
    if (v > -lim && v < lim) return 32768;
    long r;
    if (v > 0) r = (long)(v - lim) * 32767 / (32767 - lim);
    else r = (long)(v + lim) * 32768 / (32768 - lim);
    r += 32768;
    if (r < 0) r = 0;
    if (r > 65535) r = 65535;
    return (uint16_t)r;
}

void joyout_windows(const JoyOutSettings& s, const PadInput in[2], const PadOutput& port, SC55W_SHARED* sh)
{
    if (!sh) return;
    for (int k = 0; k < SC55W_NWJOY; k++) {
        SC55W_WJOY& w = sh->wjoy[k];
        const PadInput& p = in[k];
        if (!s.nuked || !p.present) { w.present = 0; continue; }
        uint32_t bt = 0;
        if (s.mode == 1) {                         // the game port as the DOS box has it
            w.axis[0] = port.joy_axis[k * 2]; w.axis[1] = port.joy_axis[k * 2 + 1];
            w.axis[2] = k == 0 ? port.joy_axis[2] : 32768; w.axis[3] = k == 0 ? port.joy_axis[3] : 32768;
            w.axis[4] = w.axis[5] = 32768;
            bt = k == 0 ? (port.joy_buttons & 0x0F) : (port.joy_buttons >> 2 & 3);
            w.pov = 0xFFFF;
            w.nbuttons = 4;
        } else {                                   // a full pad
            const bool* b = p.button;
            w.axis[0] = dz_axis(p.axis[PA_LX], s.deadzone);
            w.axis[1] = dz_axis(p.axis[PA_LY], s.deadzone);
            if (p.trig) w.axis[2] = (uint16_t)(p.trig + 32768);        // as the pad's driver has it
            else w.axis[2] = (uint16_t)(32768 + (b[PB_LT] ? 32767 : 0) - (b[PB_RT] ? 32768 : 0));
            w.axis[3] = dz_axis(p.axis[PA_RY], s.deadzone);
            w.axis[4] = dz_axis(p.axis[PA_RX], s.deadzone);
            w.axis[5] = 32768;
            static const int order[12] = {PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_BACK, PB_START, PB_LS, PB_RS,
                                           PB_LT, PB_RT};
            for (int i = 0; i < 12; i++) if (b[order[i]]) bt |= 1u << i;
            bool u = b[PB_DUP], d = b[PB_DDOWN], l = b[PB_DLEFT], r = b[PB_DRIGHT];
            w.pov = u ? (r ? 4500 : l ? 31500 : 0) : d ? (r ? 13500 : l ? 22500 : 18000) : r ? 9000 : l ? 27000 : 0xFFFF;
            w.nbuttons = 12;
        }
        w.buttons = bt;
        w.present = 1;
    }
}
