
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <dos.h>
#include <string.h>
#include <stdlib.h>

#include <dpmi/dbgutil.h>
#include "vmpu.h"

#if SBEMU_VMPU

#define TSF_IMPLEMENTATION
#define TSF_FLOAT_LUT
#include "tsf.h"
#include "sf_fx.h"

static unsigned char fpu_buffer[512] __attribute__((aligned(16)));

/* 0x330: data port
 * 0x331: read: status port
 *       write: command port
 * status port:
 * bit 6: 0=ready to write cmd or MIDI data; 1=interface busy
 * bit 7: 0=data ready to read; 1=no data at data port
 * command port:
 *  0xff: reset - triggers ACK (FE) to be read from data port
 *  0x3f: set to UART mode - triggers ACK (FE) to be read from data port
 */

static bool bReset = false;
static uint8_t bUART = 0;

static const int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};
static unsigned char midi_buffer[4096];
static unsigned int midi_ptr = 0;
static unsigned int midi_available_ptr = 0;
static unsigned int midi_message_cntr = 0;
static bool midi_in_sysex = false;
static unsigned char midi_mpu_status = 0x80;
static const unsigned char gm_reset[6] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
static const unsigned char gs_reset[11] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};

static char loaded_sf2[512];
static tsf *tsfrenderer;

/* master volume from the song (GM / GS SysEx) times the user's /VMIDIVOL; tsf_set_volume uses the FPU,
 * so the user's part is applied in VMPU_GenSamples (FPU saved), not when the option changes */
static float VMPU_SFMaster = 1.0f;
static volatile int VMPU_SFGainPct = 100, VMPU_SFGainDirty = 1;
static void VMPU_SFVolume(float v) { VMPU_SFMaster = v; if (tsfrenderer) tsf_set_volume(tsfrenderer, v * VMPU_SFGainPct / 100.0f); }
void VMPU_SetSFGain(int percent) { VMPU_SFGainPct = percent; VMPU_SFGainDirty = 1; }
static int VMPU_base;

/* ---- reverb / chorus for the SoundFont synth (settings from main.c, sends from MIDI CC 91 / 93) ---- */
static SFFX VMPU_fx;
static int VMPU_fx_rate;                       /* sample rate the effect buffers were built for, 0 = not built */
static volatile int VMPU_fx_ok;                /* 1 = VMPU_fx is valid and can be used by the audio interrupt */
static int fxs_rev = 0, fxs_room = 60, fxs_damp = 50, fxs_cho = 0, fxs_rate10 = 8, fxs_depth = 50, fxs_sends = 1;
static unsigned char VMPU_sendrev[16], VMPU_sendcho[16];

static void VMPU_FxResetSends(void)
{
    int i;
    for (i = 0; i < 16; i++) { VMPU_sendrev[i] = 40; VMPU_sendcho[i] = 24; }
}

void VMPU_SetSFEffects(int rev, int room, int damp, int cho, int rate10, int depth, int sends)
{
    VMPU_fx_ok = 0;
    fxs_rev = rev; fxs_room = room; fxs_damp = damp; fxs_cho = cho; fxs_rate10 = rate10; fxs_depth = depth; fxs_sends = sends;
    VMPU_fx_rate = 0; /* rebuilt on the next render */
    VMPU_FxResetSends();
}

static void VMPU_Process_Messages(void)
{
    unsigned char *temp_buffer = midi_buffer;
    unsigned int index = 0;

    if(index < midi_available_ptr)
    {
        #if _LOG_ENABLE
        DBG_Logi("[");
        for(int i = 0; i < midi_available_ptr; ++i)
            DBG_Logi("%x ", temp_buffer[i]);
        DBG_Logi("]");
        #endif
    }
    else
        return;

    while (index < midi_available_ptr)
    {
        _LOG("MIDI:%x, i=%d, a=%d\n", *temp_buffer, index, midi_available_ptr);
        switch (*temp_buffer & 0xF0)
        {
        case 0xD0:
            tsf_channel_set_pressure(tsfrenderer, temp_buffer[0] & 0xf, temp_buffer[1] / 127.f);
        case 0xA0:
        {
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        }
        case 0x80:
            tsf_channel_note_off(tsfrenderer, temp_buffer[0] & 0xf, temp_buffer[1]);
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        case 0x90:
            tsf_channel_note_on(tsfrenderer, temp_buffer[0] & 0xf, temp_buffer[1], temp_buffer[2] / 127.0f);
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        case 0xE0:
            tsf_channel_set_pitchwheel(tsfrenderer, temp_buffer[0] & 0xf, (temp_buffer[1] & 0x7f) | ((temp_buffer[2] & 0x7f) << 7));
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        case 0xC0:
            tsf_channel_set_presetnumber(tsfrenderer, temp_buffer[0] & 0xf, temp_buffer[1], (temp_buffer[0] & 0xf) == 0x9);
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        case 0xB0:
            if (temp_buffer[1] == 91) VMPU_sendrev[temp_buffer[0] & 0xf] = temp_buffer[2];
            else if (temp_buffer[1] == 93) VMPU_sendcho[temp_buffer[0] & 0xf] = temp_buffer[2];
            else if (temp_buffer[1] == 121) { VMPU_sendrev[temp_buffer[0] & 0xf] = 40; VMPU_sendcho[temp_buffer[0] & 0xf] = 24; }
            tsf_channel_midi_control(tsfrenderer, temp_buffer[0] & 0xf, temp_buffer[1], temp_buffer[2]);
            index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            break;
        case 0xF0:
        {
            if (*temp_buffer == 0xFF)
            {
                {
                    tsf_reset(tsfrenderer);
                    VMPU_FxResetSends();
                    tsf_channel_set_bank_preset(tsfrenderer, 9, 128, 0);
                    VMPU_SFVolume( 1.0f);
                }
            }
            if (*temp_buffer == 0xF0)
            {
                unsigned char *sysexbuf = temp_buffer;
                unsigned int sysexlen = 0, i = 0;
                while (*temp_buffer != 0xF7)
                {
                    index++;
                    temp_buffer += 1;
                    sysexlen++;
                }

                index++;
                temp_buffer += 1;
                sysexlen++;

                //						_dprintf("SYSEX MESSAGE: ");
                //						for (i = 0; i < sysexlen; i++) {
                //							_dprintf("0x%02X ", sysexbuf[i]);
                //						}
                //						_dprintf("\n");

                if (sysexbuf[1] == 0x41 && sysexbuf[3] == 0x42 && sysexbuf[4] == 0x12 && sysexlen >= 9)
                {
                    uint32_t addr = ((uint32_t)sysexbuf[5] << 16) + ((uint32_t)sysexbuf[6] << 8) + (uint32_t)sysexbuf[7];
                    if (addr == 0x400004 && tsfrenderer)
                    {
                        VMPU_SFVolume( ((sysexbuf[8] > 127) ? 127 : sysexbuf[8]) / 127.f);
                    }
                }
                if (tsfrenderer && sysexbuf[1] == 0x7f && sysexbuf[2] == 0x7f && sysexbuf[3] == 0x04 && sysexbuf[4] == 0x01)
                {
                    //                                                        _dprintf("GM Master Vol 0x%02X\n", sysexbuf[6]);
                    VMPU_SFVolume( sysexbuf[6] / 127.f);
                }
                // TODO: Differentiate between GS and GM Resets.
                if (!memcmp(sysexbuf, gs_reset, sizeof(gs_reset)) || !memcmp(sysexbuf, gm_reset, sizeof(gm_reset)))
                {
                    tsf_reset(tsfrenderer);
                    VMPU_FxResetSends();
                    tsf_channel_set_bank_preset(tsfrenderer, 9, 128, 0);
                    VMPU_SFVolume( 1.0f);
                }
            }
            else
            {
                index += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
                temp_buffer += midi_lengths[(temp_buffer[0] >> 4) - 0x8];
            }
            break;
        }
        }
    }
    //safe clamp, not needed, but just in case
    if(index > midi_available_ptr) index = midi_available_ptr;

    memmove(midi_buffer, midi_buffer + index, midi_ptr - index);
    midi_ptr -= index;
    midi_message_cntr = midi_ptr;
    midi_available_ptr -= index;
}

static void VMPU_Write(uint16_t port, uint8_t value)
////////////////////////////////////////////////////
{
    //if(VMPU_base == port)
    {
        _LOG("VMPU_Write(%X)=%X\n", port, value);
    }
    if (port == VMPU_base+1)
    {
        if (value == 0x3f)
        {
            midi_mpu_status &= ~0x80;
        }
        if (value == 0xff)
        {
            fpu_save(fpu_buffer);
            VMPU_Process_Messages();
            fpu_restore(fpu_buffer);
            bReset = true;
            midi_ptr = 0;
            midi_available_ptr = 0;
            midi_message_cntr = 0;
            midi_mpu_status &= ~0x80;
        }
        if (value == 0x05) {
            /* MPU-401 "Stop" (all channels clear): re-purpose as CC#121
             * Reset All Controllers on all 16 channels, so channel pressure
             * (aftertouch) does not bleed into the next track. */
            if (!midi_in_sysex) {
                //need handle incomplete event too? consider later
                for (uint8_t ch = 0; ch < 16 && midi_ptr + 3 <= 4092; ch++) {
                    midi_buffer[midi_ptr++] = 0xB0 | ch;
                    midi_buffer[midi_ptr++] = 0x79;
                    midi_buffer[midi_ptr++] = 0x00;
                }
                midi_available_ptr = midi_ptr;
            }
        }
    }
    else
    {
        if (!bReset)
        {
            {
                if (midi_ptr >= 4092)
                    return;

                if (midi_in_sysex)
                {
                    midi_buffer[midi_ptr++] = value;
                    if (value == 0xF7)
                    {
                        midi_available_ptr = midi_ptr;
                        midi_message_cntr = 0;
                        midi_in_sysex = false;
                    }
                    return;
                }

                //Handling incomplete messages
                //some game (e.g. PAL) have complete message like (E1, 0), (91,4A,F), where (E1,0) misses one byte data
                //most MIDI softwares discard incomplete messages
                unsigned int data_inserted = 0;
                if ((value & 0xF0) >= 0x80 && midi_message_cntr > 0)
                {
                    unsigned int lastptr = midi_ptr-1;
                    while(lastptr > 0 && (midi_buffer[lastptr] & 0xF0) < 0x80) --lastptr;
                    unsigned int lastcnt = midi_lengths[(midi_buffer[lastptr] >> 4) - 0x8];
                    if((midi_buffer[lastptr] & 0xF0) >= 0x80 && midi_ptr-lastptr < lastcnt)
                    {
                        #if 0 // 0 insertion
                        while(midi_ptr-lastptr < lastcnt)
                        {
                            _LOG("insert: p=%d,%d b=%x s=%d, c=%d\n", lastptr, midi_ptr, midi_buffer[lastptr], midi_lengths[(midi_buffer[lastptr] >> 4) - 0x8], midi_ptr-lastptr);
                            midi_buffer[midi_ptr++] = 0;
                            ++midi_message_cntr;
                            ++data_inserted;
                        }
                        #else // discard
                        midi_ptr = lastptr;
                        midi_message_cntr = 0;
                        #endif
                    }
                }

                midi_buffer[midi_ptr++] = value;
                midi_message_cntr++;

                unsigned int cnt = midi_lengths[(midi_buffer[midi_available_ptr] >> 4) - 0x8];
                if (midi_message_cntr >= cnt)
                {
                    if (value == 0xF0)
                    {
                        midi_message_cntr = 0;
                        midi_in_sysex = true;
                        return;
                    }
                    midi_available_ptr = midi_ptr - data_inserted;
                    //_LOG("a=%d b=%x, m=%d\n", midi_available_ptr, midi_buffer[midi_available_ptr], midi_message_cntr);
                    midi_message_cntr -= cnt;
                }
            }
        }
    }
    return;
}

static uint8_t VMPU_Read(uint16_t port)
///////////////////////////////////////
{
    _LOG("VMPU_Read(%X)\n", port);
    if (port == VMPU_base)
    {
        midi_mpu_status |= 0x80;
        if (bReset)
        {
            bReset = false;
            return 0xfe;
        }
        return 0xfe; // Always return Active Sensing.
    }
    else
    {
        return midi_mpu_status | (midi_ptr >= 4092 ? 0x40 : 0);
    }
}


static int tsf_stream_linear_memory_read(struct tsf_stream_memory* m, void* ptr, unsigned int size) { if (size > m->total - m->pos) size = m->total - m->pos; DPMI_LMemcpy(DPMI_PTR2L(ptr), (uintptr_t)m->buffer+m->pos, size); m->pos += size; return size; }
static int tsf_stream_linear_memory_skip(struct tsf_stream_memory* m, unsigned int count) { if (m->pos + count > m->total) return 0; m->pos += count; return 1; }
static tsf* tsf_load_linear_memory(uint32_t linear_mem, int size)
{
	struct tsf_stream stream = { TSF_NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_linear_memory_read, (int(*)(void*,unsigned int))&tsf_stream_linear_memory_skip };
	struct tsf_stream_memory f = { 0, 0, 0 };
	f.buffer = (const char*)linear_mem;
	f.total = size;
	stream.data = &f;
	return tsf_load(&stream);
}

BOOL VMPU_Init(int baseaddr, int* voices, int freq, const char* sf2)
{
    VMPU_base = 0x330;
    if (*voices < 8)
        *voices = 8;
    if (*voices > 256)
        *voices = 256;
    
    tsfrenderer = tsf_load_filename(sf2); VMPU_SFMaster = 1.0f; VMPU_SFGainDirty = 1;

    if (tsfrenderer)
    {
        int channel = 0;
        tsf_set_max_voices(tsfrenderer, 256); //pre-alloc
        tsf_set_max_voices(tsfrenderer, *voices);
        tsf_set_output(tsfrenderer, TSF_STEREO_INTERLEAVED, freq, 0);
        for (channel = 15; channel >= 0; --channel)
            tsf_channel_midi_control(tsfrenderer, channel, 121, 0);
        tsf_channel_set_bank_preset(tsfrenderer, 9, 128, 0);
        VMPU_FxResetSends();

        VMPU_base = baseaddr;

        memcpy(loaded_sf2, sf2, min(strlen(sf2)+1, sizeof(loaded_sf2)));
        loaded_sf2[sizeof(loaded_sf2)-1] = '\0';
        return TRUE;
    }
    else
    {
        memset(loaded_sf2, 0, sizeof(loaded_sf2));
        return FALSE;
    }
}

BOOL VMPU_Reset(int baseaddr, int* voices, int freq, const char* sf2, uint32_t sf2_linear_mem, int bytes)
{
    VMPU_base = 0x330;
    if (*voices < 8)
        *voices = 8;
    if (*voices > 256)
        *voices = 256;

    if(tsfrenderer && stricmp(loaded_sf2, sf2) != 0)
    {
        tsf_close(tsfrenderer);
        tsfrenderer = NULL;
    }

    if(!tsfrenderer)
        tsfrenderer = tsf_load_linear_memory(sf2_linear_mem, bytes); VMPU_SFMaster = 1.0f; VMPU_SFGainDirty = 1;

    if (tsfrenderer)
    {
        int channel = 0;
        tsf_set_max_voices(tsfrenderer, 256); //pre-alloc
        tsf_set_max_voices(tsfrenderer, *voices);
        tsf_set_output(tsfrenderer, TSF_STEREO_INTERLEAVED, freq, 0);
        for (channel = 15; channel >= 0; --channel)
            tsf_channel_midi_control(tsfrenderer, channel, 121, 0);
        tsf_channel_set_bank_preset(tsfrenderer, 9, 128, 0);
        VMPU_FxResetSends();

        VMPU_base = baseaddr;

        memcpy(loaded_sf2, sf2, min(strlen(sf2)+1, sizeof(loaded_sf2)));
        loaded_sf2[sizeof(loaded_sf2)-1] = '\0';
        return TRUE;
    }
    else
    {
        memset(loaded_sf2, 0, sizeof(loaded_sf2));
        return FALSE;
    }
}

BOOL VMPU_IsActive()
{
    if(!tsfrenderer) return FALSE;
    fpu_save(fpu_buffer);
    VMPU_Process_Messages();
    fpu_restore(fpu_buffer);
    return !!tsf_active_voice_count(tsfrenderer);
}


/* Same as tsf_render_short, but voices are also sent to the reverb / chorus buses
 * (level = global setting x the song's CC91/CC93 for the voice's channel). */
static void VMPU_RenderFX(int16_t* pcm16, int samples, int freq, BOOL domix)
{
    enum { B = 128 };
    float dry[B * 2], rb[B * 2], cb[B * 2], tmp[B * 2];
    const int do_rev = fxs_rev > 0, do_cho = fxs_cho > 0;
    if (!VMPU_fx_ok || VMPU_fx_rate != freq)
    {
        VMPU_fx_ok = 0;
        SFFX_Init(&VMPU_fx, freq, fxs_rev, fxs_room, fxs_damp, fxs_cho, fxs_rate10, fxs_depth, fxs_sends);
        VMPU_fx_rate = freq;
        VMPU_fx_ok = 1;
    }
    while (samples > 0)
    {
        int n = samples > B ? B : samples, i;
        struct tsf_voice *v = tsfrenderer->voices, *vEnd = v + tsfrenderer->voiceNum;
        memset(dry, 0, sizeof(float) * 2 * n);
        memset(rb, 0, sizeof(float) * 2 * n);
        memset(cb, 0, sizeof(float) * 2 * n);
        for (; v != vEnd; v++)
        {
            float sr, sc;
            int ch;
            if (v->playingPreset == -1) continue;
            ch = v->playingChannel & 15;
            if (fxs_sends)
            {
                sr = do_rev ? VMPU_sendrev[ch] / 64.0f : 0.0f; if (sr > 2.0f) sr = 2.0f;
                sc = do_cho ? VMPU_sendcho[ch] / 64.0f : 0.0f; if (sc > 2.0f) sc = 2.0f;
            }
            else { sr = do_rev ? 1.0f : 0.0f; sc = do_cho ? 1.0f : 0.0f; }
            if (sr == 0.0f && sc == 0.0f)
            {
                tsf_voice_render(tsfrenderer, v, dry, n);
                continue;
            }
            memset(tmp, 0, sizeof(float) * 2 * n);
            tsf_voice_render(tsfrenderer, v, tmp, n);
            for (i = 0; i < n * 2; i++)
            {
                float x = tmp[i];
                dry[i] += x;
                rb[i] += x * sr;
                cb[i] += x * sc;
            }
        }
        SFFX_Process(&VMPU_fx, rb, cb, dry, n, do_rev, do_cho);
        for (i = 0; i < n * 2; i++)
        {
            float x = dry[i];
            int vi = (x < -1.00004566f ? (int)-32768 : (x > 1.00001514f ? (int)32767 : (int)(x * 32767.5f)));
            if (domix) vi += pcm16[i];
            pcm16[i] = (int16_t)(vi < -32768 ? -32768 : (vi > 32767 ? 32767 : vi));
        }
        pcm16 += n * 2;
        samples -= n;
    }
}

void VMPU_GenSamples(int16_t* pcm16, int samples, int freq, BOOL domix)
{
    if (tsfrenderer)
    {
        fpu_save(fpu_buffer);
        VMPU_Process_Messages();
        if (VMPU_SFGainDirty) { VMPU_SFGainDirty = 0; tsf_set_volume(tsfrenderer, VMPU_SFMaster * VMPU_SFGainPct / 100.0f); }
        tsf_set_samplerate_output(tsfrenderer, freq);
        if (fxs_rev > 0 || fxs_cho > 0)
            VMPU_RenderFX(pcm16, samples, freq, domix);
        else
            tsf_render_short(tsfrenderer, pcm16, samples, domix);
        fpu_restore(fpu_buffer);
    }
}

/* SB-MIDI data written with DSP cmd 0x38 */

void VMPU_SBMidi_RawWrite(uint8_t value)
//////////////////////////////////////////
{
}

uint32_t VMPU_MPU(uint32_t port, uint32_t val, uint32_t out)
////////////////////////////////////////////////////////////
{
    return out ? (VMPU_Write(port, val), val) : (val &= ~0xff, val |= VMPU_Read(port));
}
#endif
