/* sf_fx.h - reverb (Freeverb style) and stereo chorus for the SoundFont synth.
 * Header-only, included by vmpu.c. All state is static (no allocation after the TSR goes resident).
 * Input and output are interleaved stereo float buffers.
 * GPL-2.0-or-later (Freeverb itself is public domain). */
#ifndef SF_FX_H
#define SF_FX_H

#define FX_NCOMB 8
#define FX_NAP 4
#define FX_COMBMAX 1900
#define FX_APMAX 700
#define FX_CHOMAX 2400

typedef struct
{
    /* user settings, 0..100 unless noted */
    int rev_level, rev_room, rev_damp;
    int cho_level, cho_rate10 /* tenths of Hz */, cho_depth;
    int use_sends; /* 1: scale by the song's CC91 / CC93 per channel */

    /* derived */
    float rev_wet, rev_fb, rev_damp1, rev_damp2;
    float cho_wet, cho_inc, cho_depth_smp, cho_base_smp;

    int rate;
    int comb_len[2][FX_NCOMB], ap_len[2][FX_NAP];
    int comb_pos[2][FX_NCOMB], ap_pos[2][FX_NAP];
    float comb_store[2][FX_NCOMB];
    float comb_buf[2][FX_NCOMB][FX_COMBMAX];
    float ap_buf[2][FX_NAP][FX_APMAX];

    float cho_buf[FX_CHOMAX];
    int cho_w;
    float cho_ph;
} SFFX;

static const int fx_comb_tune[FX_NCOMB] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int fx_ap_tune[FX_NAP] = {556, 441, 341, 225};

static void SFFX_Update(SFFX* fx)
{
    float room = fx->rev_room / 100.0f, damp = fx->rev_damp / 100.0f;
    fx->rev_wet = 1.2f * fx->rev_level / 100.0f;
    fx->rev_fb = room * 0.28f + 0.7f;
    fx->rev_damp1 = damp * 0.4f;
    fx->rev_damp2 = 1.0f - fx->rev_damp1;
    fx->cho_wet = 0.8f * fx->cho_level / 100.0f;
    fx->cho_inc = (fx->cho_rate10 / 10.0f) / (float)fx->rate; /* LFO cycles per sample */
    fx->cho_base_smp = 0.012f * fx->rate;
    fx->cho_depth_smp = (0.001f + 0.004f * fx->cho_depth / 100.0f) * fx->rate;
}

static void SFFX_Init(SFFX* fx, int rate, int rev_level, int rev_room, int rev_damp, int cho_level, int cho_rate10, int cho_depth, int use_sends)
{
    int c, i, rr = rate > 48000 ? 48000 : (rate < 8000 ? 8000 : rate);
    memset(fx, 0, sizeof(*fx));
    fx->rate = rate;
    fx->rev_level = rev_level; fx->rev_room = rev_room; fx->rev_damp = rev_damp;
    fx->cho_level = cho_level; fx->cho_rate10 = cho_rate10; fx->cho_depth = cho_depth; fx->use_sends = use_sends;
    for (c = 0; c < 2; c++)
    {
        for (i = 0; i < FX_NCOMB; i++)
        {
            int n = (int)((long long)(fx_comb_tune[i] + c * 23) * rr / 44100);
            fx->comb_len[c][i] = n < 8 ? 8 : (n > FX_COMBMAX ? FX_COMBMAX : n);
        }
        for (i = 0; i < FX_NAP; i++)
        {
            int n = (int)((long long)(fx_ap_tune[i] + c * 23) * rr / 44100);
            fx->ap_len[c][i] = n < 8 ? 8 : (n > FX_APMAX ? FX_APMAX : n);
        }
    }
    SFFX_Update(fx);
}

static inline float fx_undenorm(float v) { return (v > -1e-20f && v < 1e-20f) ? 0.0f : v; }

/* revbus / chobus: interleaved stereo send buses (already scaled by the per-channel send);
 * out: interleaved stereo mix to ADD the wet signal to. */
static void SFFX_Process(SFFX* fx, const float* revbus, const float* chobus, float* out, int frames, int do_rev, int do_cho)
{
    int n;
    for (n = 0; n < frames; n++)
    {
        float wl = 0.0f, wr = 0.0f;
        if (do_rev)
        {
            float in = (revbus[2 * n] + revbus[2 * n + 1]) * 0.015f, ol = 0.0f, orr = 0.0f;
            int c, i;
            for (c = 0; c < 2; c++)
            {
                float o = 0.0f;
                for (i = 0; i < FX_NCOMB; i++)
                {
                    int p = fx->comb_pos[c][i];
                    float* b = &fx->comb_buf[c][i][p];
                    float y = *b;
                    fx->comb_store[c][i] = fx_undenorm(y * fx->rev_damp2 + fx->comb_store[c][i] * fx->rev_damp1);
                    *b = in + fx->comb_store[c][i] * fx->rev_fb;
                    if (++p >= fx->comb_len[c][i]) p = 0;
                    fx->comb_pos[c][i] = p;
                    o += y;
                }
                for (i = 0; i < FX_NAP; i++)
                {
                    int p = fx->ap_pos[c][i];
                    float* b = &fx->ap_buf[c][i][p];
                    float bo = *b;
                    float y = -o + bo;
                    *b = o + bo * 0.5f;
                    if (++p >= fx->ap_len[c][i]) p = 0;
                    fx->ap_pos[c][i] = p;
                    o = y;
                }
                if (c == 0) ol = o; else orr = o;
            }
            wl += ol * fx->rev_wet;
            wr += orr * fx->rev_wet;
        }
        if (do_cho)
        {
            float x = (chobus[2 * n] + chobus[2 * n + 1]) * 0.5f;
            float ph = fx->cho_ph, ph2 = ph + 0.5f, t1, t2, d1, d2, r1, r2;
            int i1, i2;
            if (ph2 >= 1.0f) ph2 -= 1.0f;
            t1 = ph < 0.5f ? (ph * 4.0f - 1.0f) : (3.0f - ph * 4.0f); /* triangle -1..1 */
            t2 = ph2 < 0.5f ? (ph2 * 4.0f - 1.0f) : (3.0f - ph2 * 4.0f);
            fx->cho_buf[fx->cho_w] = x;
            d1 = fx->cho_base_smp + t1 * fx->cho_depth_smp;
            d2 = fx->cho_base_smp + t2 * fx->cho_depth_smp;
            { /* linear interpolation on a ring buffer */
                float p1 = (float)fx->cho_w - d1, p2 = (float)fx->cho_w - d2, f1, f2;
                if (p1 < 0) p1 += FX_CHOMAX;
                if (p2 < 0) p2 += FX_CHOMAX;
                i1 = (int)p1; f1 = p1 - i1; i2 = (int)p2; f2 = p2 - i2;
                { int j1 = i1 + 1 >= FX_CHOMAX ? 0 : i1 + 1, j2 = i2 + 1 >= FX_CHOMAX ? 0 : i2 + 1;
                  r1 = fx->cho_buf[i1] * (1 - f1) + fx->cho_buf[j1] * f1;
                  r2 = fx->cho_buf[i2] * (1 - f2) + fx->cho_buf[j2] * f2; }
            }
            if (++fx->cho_w >= FX_CHOMAX) fx->cho_w = 0;
            ph += fx->cho_inc; if (ph >= 1.0f) ph -= 1.0f; fx->cho_ph = ph;
            wl += r1 * fx->cho_wet;
            wr += r2 * fx->cho_wet;
        }
        out[2 * n] += wl;
        out[2 * n + 1] += wr;
    }
}
#endif
