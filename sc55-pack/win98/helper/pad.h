// pad.h - game pad -> keys / game port for DOS boxes, with a layout per game (PADMAP.TXT)
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// one read of the pad, in Xbox names
enum PadButton { PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT, PB_LS, PB_RS, PB_BACK, PB_START,
                 PB_DUP, PB_DDOWN, PB_DLEFT, PB_DRIGHT, PB_COUNT };
enum PadAxis { PA_LX, PA_LY, PA_RX, PA_RY, PA_COUNT };

struct PadInput {
    bool present = false;
    bool button[PB_COUNT] = {};
    int axis[PA_COUNT] = {};           // -32768..32767, 0 = centre
    int trig = 0;                      // the Windows axis the triggers are on (LT +, RT -), 0 = none
};

// what the DOS box gets
struct PadOutput {
    std::vector<uint8_t> scancodes;    // set-1 make/break codes to send now
    uint16_t joy_axis[4] = {32768, 32768, 32768, 32768};
    uint8_t joy_buttons = 0;
    uint8_t joy_present = 0;
};

// load PADMAP.TXT (generated) and PADUSER.TXT (the user's, wins); returns the number of games
int pad_load(const std::string& padmap, const std::string& paduser, std::string& log);

// pick the layout for the programs running in the focused DOS box (newest last); "" = default
// returns the layout's title ("Default" when none matched); where = the file that matched
std::string pad_select(const std::vector<std::pair<std::string, uint32_t>>& progs, std::string& where);

// one step: input -> output (keys only for changes); focus_dos = a DOS box has the keyboard
void pad_step(const PadInput& in, bool focus_dos, PadOutput& out);

// release everything (keys sent as breaks into out)
void pad_release(PadOutput& out);

// names
const char* pad_button_name(int b);
uint32_t pad_file_id(const std::string& name83, uint32_t size);

// the converter (padconv.c): keyb2joypad.cpp -> PADMAP.TXT; 0 = ok, msg = result or error
extern "C" int padconv_run(const char* in, const char* out, char* msg, int msglen);
