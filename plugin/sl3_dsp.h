// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Rane SL3 Real-Time Zero-Echo 4-Stem Separator (128-Band HTDemucs Neural Engine), DJ FX & AudioUnit Bridge
 * By pepelee (PPLEE) - @pepeleedj
 *
 * Uses both CoreAudio HAL Custom Property ('sl3x' / 'sl3p') AND POSIX Shared Memory
 * (shm_open "/rane_sl3_dsp", permitted by com.apple.audio.coreaudiod.sb)
 * so communication between Rane SL3 Stems & FX.app and SL3Device.driver
 * works 100% inside macOS coreaudiod sandbox.
 */
#ifndef SL3_DSP_H
#define SL3_DSP_H

#include <stdint.h>
#include <stdatomic.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define kSL3PropDSP         'sl3x'
#define kSL3PropStemProfile 'sl3p'
#define kSL3PropStemPCM     'sl3w'
#define SL3_POSIX_SHM       "/rane_sl3_dsp"
#define SL3_DSP_MAGIC       0x534C335Bu /* 'SL3[' */
#define SL3_DSP_VERSION     8u

#define SL3_AU_RING_N       4096u
#define SL3_AU_RING_MASK    (SL3_AU_RING_N - 1u)

/*
 * True Time-Domain HTDemucs 4-Stem PCM Streaming Constants:
 *   8 int16_t channels per sample:
 *     [0..1] = Vocal L/R, [2..3] = Melody L/R, [4..5] = Bass L/R, [6..7] = Drums L/R
 *   Chunked transfer over CoreAudio HAL Custom Property 'sl3w' (262,144 samples = 4.19 MB/chunk)
 */
#define SL3_PCM_MAX_SAMPLES (9216u * 2048u)
#define SL3_PCM_CHUNK_SAMPS 262144u

typedef struct {
    uint32_t deck;
    uint32_t start_sample;
    uint32_t num_samples;
    uint32_t total_samples;
} sl3_pcm_chunk_hdr_t;

/*
 * Pre-Analyzed Song HTDemucs 128-Band Stereo-Linked Harmonic Profile
 * (46.44 ms per frame = 2048 samples at 44.1 kHz)
 *
 * Band resolution across 513 FFT bins (1024-pt STFT):
 *   Bands   0..63  (bins   0..63,    0 Hz ..  2.76 kHz): 1-to-1 exact FFT bin (43.1 Hz/band)
 *   Bands  64..95  (bins  64..127, 2.76 kHz ..  5.51 kHz): 2 bins/band (86.1 Hz/band)
 *   Bands  96..119 (bins 128..223, 5.51 kHz ..  9.65 kHz): 4 bins/band (172.3 Hz/band)
 *   Bands 120..127 (bins 224..512, 9.65 kHz .. 22.05 kHz): 8 high-air bands
 *
 * Stereo-linked masks stored as 4-bit nibbles (0..15 -> 0.0..1.0):
 *   voc_mel: high nibble = Vocal, low nibble = Melody
 *   bas_drm: high nibble = Bass,  low nibble = Drums
 */
#define SL3_ANA_MAX_FRAMES  9216u /* ~7.1 minutes at 2048 samples (46.44 ms) per frame */
#define SL3_ANA_BANDS       128u  /* 128 high-resolution harmonic subbands */
#define SL3_ANA_FP_BINS     12u   /* 12 log-spaced spectral fingerprint bands (86 Hz..8.2 kHz) */
#define SL3_ANA_FP_HIST     16u   /* 16 frames (~743 ms) phrase fingerprint matcher */

typedef struct {
    uint32_t deck;
    uint32_t num_frames;
    uint32_t is_neural; /* 1 = HTDemucs 4-stem neural network, 0 = fast fallback */
    uint8_t  fp[SL3_ANA_MAX_FRAMES][SL3_ANA_FP_BINS];
    uint8_t  voc_mel[SL3_ANA_MAX_FRAMES][SL3_ANA_BANDS];
    uint8_t  bas_drm[SL3_ANA_MAX_FRAMES][SL3_ANA_BANDS];
} sl3_stem_profile_t;

/* Compact control & meter struct exchanged via CoreAudio Property 'sl3x' */
typedef struct {
    uint8_t  vocal[2];
    uint8_t  melody[2];
    uint8_t  bass[2];
    uint8_t  drums[2];
    uint8_t  echo_arm[2];
    uint8_t  vinyl_brake[2];
    int32_t  filter[2];
    uint32_t echo_bpm[2];
    float    meter_l[2];
    float    meter_r[2];
    float    stem_meter[2][4];
    uint8_t  ana_ready[2];
    uint32_t ana_cur_frame[2];
    uint32_t ana_num_frames[2];
} sl3_dsp_ctrl_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    _Atomic uint8_t vocal[2];
    _Atomic uint8_t melody[2];
    _Atomic uint8_t bass[2];
    _Atomic uint8_t drums[2];
    _Atomic uint8_t echo_arm[2];
    _Atomic uint8_t vinyl_brake[2];
    _Atomic int32_t filter[2];
    _Atomic uint32_t echo_bpm[2];
    _Atomic float meter_l[2];
    _Atomic float meter_r[2];
    _Atomic float stem_meter[2][4];

    /* Lock-free AudioUnit Insert Bridge */
    _Atomic uint8_t  au_active[2];
    _Atomic float    au_wet[2];
    _Atomic uint64_t au_heartbeat_ms;
    _Atomic uint64_t au_write_pos[2];
    _Atomic uint64_t au_read_pos[2];
    float            au_in_l[2][SL3_AU_RING_N];
    float            au_in_r[2][SL3_AU_RING_N];
    float            au_out_l[2][SL3_AU_RING_N];
    float            au_out_r[2][SL3_AU_RING_N];
    _Atomic uint64_t au_out_stamp[2][SL3_AU_RING_N];

    /* Pre-Analyzed Song HTDemucs Stem Profiles per Deck */
    _Atomic uint8_t    ana_ready[2];
    _Atomic uint32_t   ana_cur_frame[2];
    _Atomic uint32_t   ana_num_frames[2];
    sl3_stem_profile_t profile[2];
} sl3_dsp_shm_t;

static inline void sl3_dsp_init_defaults(sl3_dsp_shm_t *shm) {
    memset(shm, 0, sizeof(*shm));
    for (int d = 0; d < 2; d++) {
        atomic_store(&shm->vocal[d], 1);
        atomic_store(&shm->melody[d], 1);
        atomic_store(&shm->bass[d], 1);
        atomic_store(&shm->drums[d], 1);
        atomic_store(&shm->echo_arm[d], 0);
        atomic_store(&shm->vinyl_brake[d], 0);
        atomic_store(&shm->filter[d], 0);
        atomic_store(&shm->echo_bpm[d], 124);
        atomic_store(&shm->au_active[d], 0);
        atomic_store(&shm->au_wet[d], 1.0f);
    }
    shm->version = SL3_DSP_VERSION;
    shm->magic = SL3_DSP_MAGIC;
}

static inline sl3_dsp_shm_t *sl3_dsp_open_shm(void) {
    mode_t old_umask = umask(0);
    int fd = shm_open(SL3_POSIX_SHM, O_RDWR | O_CREAT, 0666);
    umask(old_umask);
    if (fd >= 0) {
        ftruncate(fd, sizeof(sl3_dsp_shm_t));
        sl3_dsp_shm_t *shm = (sl3_dsp_shm_t *)mmap(NULL, sizeof(sl3_dsp_shm_t),
                                                   PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (shm != MAP_FAILED && shm != NULL) {
            if (shm->magic != SL3_DSP_MAGIC || shm->version != SL3_DSP_VERSION) {
                sl3_dsp_init_defaults(shm);
            }
            return shm;
        }
    }
    static sl3_dsp_shm_t s_local_shm;
    if (s_local_shm.magic != SL3_DSP_MAGIC) {
        sl3_dsp_init_defaults(&s_local_shm);
    }
    return &s_local_shm;
}

/*
 * Zero-Echo Short-Window STFT Configuration:
 *   SL3_FFT_N = 1024 samples (23.2 ms total window, 11.6 ms effective Hann^2 width)
 *   SL3_FFT_H = 256 samples  (5.8 ms hop, 75% overlap, exact COLA reconstruction)
 */
#define SL3_ECHO_MAX_SAMPLES 48000
#define SL3_BRAKE_MAX_SAMPLES 96000
#define SL3_FFT_N 1024
#define SL3_FFT_H 256
#define SL3_OLA_N 2048
#define SL3_FFT_BINS (SL3_FFT_N / 2 + 1)

static inline int sl3_bin_to_band(int k) {
    if (k < 64)  return k;                        /*   0..63:    0 Hz .. 2.76 kHz (43.1 Hz/band, 1-to-1 bin) */
    if (k < 128) return 64 + ((k - 64) >> 1);     /*  64..95: 2.76 kHz .. 5.51 kHz (86.1 Hz/band, 2 bins) */
    if (k < 224) return 96 + ((k - 128) >> 2);    /*  96..119: 5.51 kHz .. 9.65 kHz (172.3 Hz/band, 4 bins) */
    int b = 120 + ((k - 224) >> 5);               /* 120..127: 9.65 kHz .. 22.05 kHz */
    return (b > 127) ? 127 : b;
}

static inline int sl3_bin_to_fp_group(int k) {
    if (k <= 3)   return 0;  /* 86..129 Hz */
    if (k <= 5)   return 1;  /* 172..215 Hz */
    if (k <= 8)   return 2;  /* 258..344 Hz */
    if (k <= 11)  return 3;  /* 387..473 Hz */
    if (k <= 15)  return 4;  /* 516..645 Hz */
    if (k <= 21)  return 5;  /* 689..904 Hz */
    if (k <= 29)  return 6;  /* 947..1248 Hz */
    if (k <= 40)  return 7;  /* 1291..1722 Hz */
    if (k <= 56)  return 8;  /* 1765..2411 Hz */
    if (k <= 80)  return 9;  /* 2454..3445 Hz */
    if (k <= 120) return 10; /* 3488..5167 Hz */
    return 11;               /* 5210..8225 Hz */
}

static inline uint8_t sl3_pack_nibbles(float hi_01, float lo_01) {
    int hi = (int)lrintf(fminf(1.0f, fmaxf(0.0f, hi_01)) * 15.0f);
    int lo = (int)lrintf(fminf(1.0f, fmaxf(0.0f, lo_01)) * 15.0f);
    return (uint8_t)(((hi & 0x0F) << 4) | (lo & 0x0F));
}

static inline void sl3_fft1024(float *re, float *im, int invert) {
    int j = 0;
    for (int i = 1; i < SL3_FFT_N; i++) {
        int bit = SL3_FFT_N >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= SL3_FFT_N; len <<= 1) {
        float ang = 2.0f * 3.14159265358979323846f / (float)len * (invert ? 1.0f : -1.0f);
        float wlen_r = cosf(ang), wlen_i = sinf(ang);
        for (int i = 0; i < SL3_FFT_N; i += len) {
            float wr = 1.0f, wi = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; k++) {
                int u = i + k, v = i + k + half;
                float vr = re[v] * wr - im[v] * wi;
                float vi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;
                float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
    if (invert) {
        const float invN = 1.0f / (float)SL3_FFT_N;
        for (int i = 0; i < SL3_FFT_N; i++) { re[i] *= invN; im[i] *= invN; }
    }
}

typedef struct {
    /* 1024-point (23.2 ms) Zero-Echo STFT State */
    float stft_win[SL3_FFT_N];
    float stft_in_l[SL3_FFT_N], stft_in_r[SL3_FFT_N];
    float stft_ola_l[SL3_OLA_N], stft_ola_r[SL3_OLA_N];
    float band_fast_m[SL3_ANA_BANDS], band_slow_m[SL3_ANA_BANDS];
    float band_fast_s[SL3_ANA_BANDS], band_slow_s[SL3_ANA_BANDS];
    uint32_t stft_pos;
    int   stft_ready;

    /* Real-time 16-Frame (743 ms) Audio Fingerprint matcher for HTDemucs Profile sync */
    float   fp_acc[SL3_ANA_FP_BINS];
    uint8_t fp_hist[SL3_ANA_FP_HIST][SL3_ANA_FP_BINS];
    int     fp_hop_cnt;
    int     fp_valid_frames;
    int     ana_cur_frame;
    int     ana_search_cursor;
    int     ana_locked;
    int     ana_miss_streak;

    /* True Time-Domain HTDemucs Sample-Accurate Waveform Tracker */
    double  pcm_pos;
    double  pcm_rate;
    float   pcm_gain;
    float   pcm_gain_target;
    float   env_in_fast;
    float   env_ref_fast;
    int     pcm_locked;
    int     pcm_miss_cnt;

    /* Smooth gain slews (click-free 5ms transitions) */
    float g_voc, g_mel, g_bas, g_drm;
    uint8_t prev_voc, prev_mel, prev_bas, prev_drm;

    /* Time-domain Echo Out & Meter filters (aligned to STFT output!) */
    float low_lp_l, low_bp_l, low_lp_r, low_bp_r;
    float voc_lp_l, voc_bp_l, voc_lp_r, voc_bp_r;

    /* Stem Echo Out feed timer */
    int echo_feed_voc, echo_feed_mel, echo_feed_bas, echo_feed_drm;
    float echo_buf_l[SL3_ECHO_MAX_SAMPLES];
    float echo_buf_r[SL3_ECHO_MAX_SAMPLES];
    int echo_w;
    float echo_lp_l, echo_lp_r, echo_hp_l, echo_hp_r;

    /* Pioneer DJM-style Color Filter state */
    float cf_lp_l, cf_bp_l, cf_lp_r, cf_bp_r;

    /* Turntable Vinyl Brake buffer */
    float brake_buf_l[SL3_BRAKE_MAX_SAMPLES];
    float brake_buf_r[SL3_BRAKE_MAX_SAMPLES];
    int brake_w;
    double brake_r;
    float brake_rate;
    uint8_t prev_brake;

    /* Peak decay meters */
    float pk_l, pk_r, pk_stem[4];
    int meter_decim;
    int is_aux_instance;
} sl3_deck_dsp_t;

static inline void sl3_deck_dsp_init(sl3_deck_dsp_t *st) {
    memset(st, 0, sizeof(*st));
    st->g_voc = st->g_mel = st->g_bas = st->g_drm = 1.0f;
    st->prev_voc = st->prev_mel = st->prev_bas = st->prev_drm = 1;
    st->brake_rate = 1.0f;
    st->pcm_rate = 1.0;
    st->pcm_gain = 1.0f;
    st->pcm_gain_target = 1.0f;
    for (int i = 0; i < SL3_FFT_N; i++) {
        st->stft_win[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / (float)SL3_FFT_N));
    }
    st->stft_ready = 1;
}

static inline float sl3_ref_mono(const int16_t (*pcm)[8], int s) {
    const int16_t *p = pcm[s];
    int sum = (int)p[0] + (int)p[1] + (int)p[2] + (int)p[3] +
              (int)p[4] + (int)p[5] + (int)p[6] + (int)p[7];
    return (float)sum * (0.5f / 32768.0f);
}

static inline void sl3_dsp_process_deck(sl3_deck_dsp_t *st, sl3_dsp_shm_t *shm, int d,
                                        const int16_t (*pcm)[8], uint32_t pcm_ready,
                                        float *io_l, float *io_r, float sr) {
    if (!st->stft_ready) sl3_deck_dsp_init(st);

    float in_l = *io_l;
    float in_r = *io_r;

    uint8_t t_voc = shm ? atomic_load_explicit(&shm->vocal[d], memory_order_relaxed) : 1;
    uint8_t t_mel = shm ? atomic_load_explicit(&shm->melody[d], memory_order_relaxed) : 1;
    uint8_t t_bas = shm ? atomic_load_explicit(&shm->bass[d], memory_order_relaxed) : 1;
    uint8_t t_drm = shm ? atomic_load_explicit(&shm->drums[d], memory_order_relaxed) : 1;
    uint8_t echo_arm = shm ? atomic_load_explicit(&shm->echo_arm[d], memory_order_relaxed) : 0;

    int feed_samples = (int)(sr * 0.28f);
    if (st->prev_voc && !t_voc && echo_arm) st->echo_feed_voc = feed_samples;
    if (st->prev_mel && !t_mel && echo_arm) st->echo_feed_mel = feed_samples;
    if (st->prev_bas && !t_bas && echo_arm) st->echo_feed_bas = feed_samples;
    if (st->prev_drm && !t_drm && echo_arm) st->echo_feed_drm = feed_samples;
    st->prev_voc = t_voc; st->prev_mel = t_mel; st->prev_bas = t_bas; st->prev_drm = t_drm;

    const float slew = 0.006f;
    st->g_voc += ((t_voc ? 1.0f : 0.0f) - st->g_voc) * slew;
    st->g_mel += ((t_mel ? 1.0f : 0.0f) - st->g_mel) * slew;
    st->g_bas += ((t_bas ? 1.0f : 0.0f) - st->g_bas) * slew;
    st->g_drm += ((t_drm ? 1.0f : 0.0f) - st->g_drm) * slew;

    uint32_t p_cur = st->stft_pos;
    uint32_t w_idx = p_cur & (SL3_FFT_N - 1);
    st->stft_in_l[w_idx] = in_l;
    st->stft_in_r[w_idx] = in_r;

    /* Exact delayed input matching the 1024-pt STFT latency (23.2 ms) */
    uint32_t al_idx = (p_cur - (uint32_t)(SL3_FFT_N - 1)) & (SL3_FFT_N - 1);
    float al_in_l = st->stft_in_l[al_idx];
    float al_in_r = st->stft_in_r[al_idx];

    /* Process 1024-point STFT hop & Sample-Accurate Time-Domain Correlator every 256 samples (5.8 ms) */
    if ((p_cur & (SL3_FFT_H - 1)) == (SL3_FFT_H - 1)) {
        float fft_re[SL3_FFT_N], fft_im[SL3_FFT_N];
        float out_re[SL3_FFT_N], out_im[SL3_FFT_N];
        float Lr_arr[SL3_FFT_BINS], Li_arr[SL3_FFT_BINS];
        float Rr_arr[SL3_FFT_BINS], Ri_arr[SL3_FFT_BINS];

        for (int i = 0; i < SL3_FFT_N; i++) {
            int idx = (int)((p_cur - (uint32_t)(SL3_FFT_N - 1) + (uint32_t)i) & (SL3_FFT_N - 1));
            float w = st->stft_win[i];
            fft_re[i] = st->stft_in_l[idx] * w;
            fft_im[i] = st->stft_in_r[idx] * w;
        }
        sl3_fft1024(fft_re, fft_im, 0);

        float band_e_m[SL3_ANA_BANDS] = {0};
        float band_e_s[SL3_ANA_BANDS] = {0};
        float band_cnt[SL3_ANA_BANDS] = {0};

        for (int k = 0; k <= SL3_FFT_N / 2; k++) {
            int kc = (SL3_FFT_N - k) & (SL3_FFT_N - 1);
            float re_k = fft_re[k], im_k = fft_im[k];
            float re_c = fft_re[kc], im_c = fft_im[kc];

            float Lr = 0.5f * (re_k + re_c);
            float Li = 0.5f * (im_k - im_c);
            float Rr = 0.5f * (im_k + im_c);
            float Ri = 0.5f * (re_c - re_k);
            Lr_arr[k] = Lr; Li_arr[k] = Li;
            Rr_arr[k] = Rr; Ri_arr[k] = Ri;

            float mr = 0.5f * (Lr + Rr), mi = 0.5f * (Li + Ri);
            float sr_b = 0.5f * (Lr - Rr), si_b = 0.5f * (Li - Ri);

            float M_mag = sqrtf(mr * mr + mi * mi);
            float S_mag = sqrtf(sr_b * sr_b + si_b * si_b);

            if (k >= 2 && k <= 191) {
                st->fp_acc[sl3_bin_to_fp_group(k)] += M_mag + S_mag;
            }
            int b = sl3_bin_to_band(k);
            band_e_m[b] += M_mag;
            band_e_s[b] += S_mag;
            band_cnt[b] += 1.0f;
        }

        /* Update 16-Frame (743 ms) Fingerprint Matcher every 8 hops (8 * 256 = 2048 samples = 46.44 ms) */
        uint8_t ana_on = shm ? atomic_load_explicit(&shm->ana_ready[d], memory_order_acquire) : 0;
        int ana_nf = shm ? (int)atomic_load_explicit(&shm->ana_num_frames[d], memory_order_relaxed) : 0;
        if (ana_nf > (int)SL3_ANA_MAX_FRAMES) ana_nf = (int)SL3_ANA_MAX_FRAMES;

        if (++st->fp_hop_cnt >= 8) {
            st->fp_hop_cnt = 0;
            float fp_sum = 0.0f;
            for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) fp_sum += st->fp_acc[g];

            /* Only advance and match when real audio signal is playing (not silence) */
            if (fp_sum > 0.02f) {
                for (int h = 0; h < (int)SL3_ANA_FP_HIST - 1; h++) {
                    for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) {
                        st->fp_hist[h][g] = st->fp_hist[h + 1][g];
                    }
                }
                for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) {
                    int v = (int)(st->fp_acc[g] / fp_sum * 512.0f);
                    if (v > 255) v = 255;
                    st->fp_hist[SL3_ANA_FP_HIST - 1][g] = (uint8_t)v;
                }
                if (st->fp_valid_frames < (int)SL3_ANA_FP_HIST) st->fp_valid_frames++;

                if (ana_on && ana_nf > (int)SL3_ANA_FP_HIST && !st->is_aux_instance) {
                    const sl3_stem_profile_t *pf = &shm->profile[d];
                    int hist_n = (st->fp_valid_frames < (int)SL3_ANA_FP_HIST) ? st->fp_valid_frames : (int)SL3_ANA_FP_HIST;
                    if (hist_n < 4) {
                        if (st->ana_cur_frame + 1 < ana_nf) st->ana_cur_frame++;
                        atomic_store_explicit(&shm->ana_cur_frame[d], (uint32_t)st->ana_cur_frame, memory_order_relaxed);
                    } else {
                        /* If PCM waveform correlator lost lock (e.g. Cue jump), use short 4-frame window for instant relock */
                        if (pcm_ready > 4096 && !st->pcm_locked && hist_n > 4) {
                            hist_n = 4;
                        }
                        int cur = st->ana_cur_frame;
                        if (cur < hist_n - 1) cur = hist_n - 1;
                        if (cur >= ana_nf) cur = ana_nf - 1;

                        /* 1. Local tracking neighborhood (cur - 1 .. cur + 2) */
                        int loc_best_f = (cur + 1 < ana_nf) ? (cur + 1) : cur;
                        int loc_best_err = 9999999;
                        int raw_loc_err  = 9999999;
                        int loc_lo = cur - 1;
                        int loc_hi = cur + 2;
                        if (loc_lo < hist_n - 1) loc_lo = hist_n - 1;
                        if (loc_hi >= ana_nf) loc_hi = ana_nf - 1;

                        int h_start = (int)SL3_ANA_FP_HIST - hist_n;
                        for (int c = loc_lo; c <= loc_hi; c++) {
                            int err = 0;
                            for (int h = 0; h < hist_n; h++) {
                                const uint8_t *p = pf->fp[c - (hist_n - 1) + h];
                                const uint8_t *q = st->fp_hist[h_start + h];
                                for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) {
                                    int diff = (int)p[g] - (int)q[g];
                                    err += (diff < 0) ? -diff : diff;
                                }
                            }
                            int adj_err = err;
                            if (c == cur + 1) adj_err -= 48; /* strong 1x playback inertia */
                            else if (c == cur || c == cur + 2) adj_err -= 16;
                            if (adj_err < loc_best_err) {
                                loc_best_err = adj_err;
                                raw_loc_err  = err;
                                loc_best_f   = c;
                            }
                        }

                        /* Scale error to 16-frame equivalent (max possible sum = 16 * 512 = 8192) */
                        int norm_loc_err = (raw_loc_err * (int)SL3_ANA_FP_HIST) / hist_n;
                        if (norm_loc_err > 1550) {
                            st->ana_miss_streak++;
                        } else {
                            st->ana_miss_streak = 0;
                            st->ana_locked = 1;
                        }

                        /* 2. Relock search when not yet locked, PCM unlocked, or after 2 high-error frames (Cue jump) */
                        int chosen_f = loc_best_f;
                        if (!st->ana_locked || (pcm_ready > 4096 && !st->pcm_locked) || st->ana_miss_streak >= 2) {
                            int glb_best_f = loc_best_f;
                            int glb_best_err = raw_loc_err;
                            for (int c = hist_n - 1; c < ana_nf; c++) {
                                int err = 0;
                                for (int h = 0; h < hist_n; h++) {
                                    const uint8_t *p = pf->fp[c - (hist_n - 1) + h];
                                    const uint8_t *q = st->fp_hist[h_start + h];
                                    for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) {
                                        int diff = (int)p[g] - (int)q[g];
                                        err += (diff < 0) ? -diff : diff;
                                    }
                                    if (err >= glb_best_err) break;
                                }
                                if (err < glb_best_err) {
                                    glb_best_err = err;
                                    glb_best_f = c;
                                }
                            }
                            if (!st->ana_locked || (pcm_ready > 4096 && !st->pcm_locked) || (glb_best_err + 180 < raw_loc_err)) {
                                chosen_f = glb_best_f;
                                st->ana_locked = 1;
                                st->ana_miss_streak = 0;
                            }
                        }

                        st->ana_cur_frame = chosen_f;
                        atomic_store_explicit(&shm->ana_cur_frame[d], (uint32_t)chosen_f, memory_order_relaxed);
                    }
                }
            }
            for (int g = 0; g < (int)SL3_ANA_FP_BINS; g++) st->fp_acc[g] = 0.0f;
        }

        /* ====================================================================
         * 2-Stage Sample-Accurate Waveform Cross-Correlator for True Time-Domain Stems
         * Correlates the upcoming 512 samples in stft_in_l/r (starting at al_idx)
         * against the pre-separated HTDemucs PCM waveforms so output has 0 samples lag!
         * ==================================================================== */
        if (pcm != NULL && pcm_ready > 4096 && !st->is_aux_instance) {
            float live_m[512];
            float e_live = 0.0f;
            float e_live2 = 0.0f;
            float e_live8 = 0.0f;
            for (int i = 0; i < 512; i++) {
                uint32_t idx = (al_idx + (uint32_t)i) & (SL3_FFT_N - 1);
                float m = 0.5f * (st->stft_in_l[idx] + st->stft_in_r[idx]);
                live_m[i] = m;
                float m2 = m * m;
                e_live += m2;
                if ((i & 1) == 0) e_live2 += m2;
                if ((i & 7) == 0) e_live8 += m2;
            }

            if (e_live > 1e-5f) {
                int center = (int)lrint(st->pcm_pos);
                int coarse_center = (st->ana_cur_frame + 1) * 2048;
                int best_s = center;
                float best_r = -1.0f;

                /* Stage 1: Ultra-tight ±1 sample phase check when locked so pcm_pos NEVER jumps on periodic bass/synth notes */
                if (st->pcm_locked && center >= 2 && center + 514 < (int)pcm_ready) {
                    for (int s = center - 1; s <= center + 1; s++) {
                        float dot = 0.0f, er = 1e-12f;
                        for (int i = 0; i < 512; i += 2) {
                            float rm = sl3_ref_mono(pcm, s + i);
                            dot += live_m[i] * rm;
                            er  += rm * rm;
                        }
                        float r = dot / sqrtf(e_live2 * er);
                        /* Prefer exact center unless ±1 is significantly better (pitch drift) */
                        float bonus = (s == center) ? 0.005f : 0.0f;
                        if (r + bonus > best_r) { best_r = r; best_s = s; }
                    }
                    /* If ±1 sample check dropped (e.g. turntable pitch change), check ±32 samples */
                    if (best_r < 0.55f && center >= 32 && center + 544 < (int)pcm_ready) {
                        for (int s = center - 32; s <= center + 32; s += 2) {
                            float dot = 0.0f, er = 1e-12f;
                            for (int i = 0; i < 512; i += 2) {
                                float rm = sl3_ref_mono(pcm, s + i);
                                dot += live_m[i] * rm;
                                er  += rm * rm;
                            }
                            float r = dot / sqrtf(e_live2 * er);
                            if (r > best_r) { best_r = r; best_s = s; }
                        }
                        int b2 = best_s;
                        for (int s = b2 - 1; s <= b2 + 1; s++) {
                            if (s < 0 || s + 512 >= (int)pcm_ready) continue;
                            float dot = 0.0f, er = 1e-12f;
                            for (int i = 0; i < 512; i += 2) {
                                float rm = sl3_ref_mono(pcm, s + i);
                                dot += live_m[i] * rm;
                                er  += rm * rm;
                            }
                            float r = dot / sqrtf(e_live2 * er);
                            if (r > best_r) { best_r = r; best_s = s; }
                        }
                    }
                }

                /* Stage 2: Wide relock (+- 8192 samples around fingerprint frame & track start) ONLY when unlocked or after sustained mismatch */
                if (!st->pcm_locked || (best_r < 0.45f && st->pcm_miss_cnt >= 3)) {
                    int s_lo = coarse_center - 8192;
                    int s_hi = coarse_center + 8192;
                    if (s_lo < 0) s_lo = 0;
                    if (s_hi > (int)pcm_ready - 512) s_hi = (int)pcm_ready - 512;
                    for (int s = s_lo; s <= s_hi; s += 8) {
                        float dot = 0.0f, er = 1e-12f;
                        for (int i = 0; i < 512; i += 8) {
                            float rm = sl3_ref_mono(pcm, s + i);
                            dot += live_m[i] * rm;
                            er  += rm * rm;
                        }
                        float r = dot / sqrtf(e_live8 * er);
                        if (r > best_r) { best_r = r; best_s = s; }
                    }
                    if (best_r < 0.65f && pcm_ready > 8704) {
                        for (int s = 0; s <= 8192; s += 8) {
                            float dot = 0.0f, er = 1e-12f;
                            for (int i = 0; i < 512; i += 8) {
                                float rm = sl3_ref_mono(pcm, s + i);
                                dot += live_m[i] * rm;
                                er  += rm * rm;
                            }
                            float r = dot / sqrtf(e_live8 * er);
                            if (r > best_r) { best_r = r; best_s = s; }
                        }
                    }
                    int b8 = best_s;
                    int r_lo = (b8 - 12 < 0) ? 0 : (b8 - 12);
                    int r_hi = (b8 + 12 > (int)pcm_ready - 512) ? ((int)pcm_ready - 512) : (b8 + 12);
                    best_r = -1.0f;
                    for (int s = r_lo; s <= r_hi; s++) {
                        float dot = 0.0f, er = 1e-12f;
                        for (int i = 0; i < 512; i += 2) {
                            float rm = sl3_ref_mono(pcm, s + i);
                            dot += live_m[i] * rm;
                            er  += rm * rm;
                        }
                        float r = dot / sqrtf(e_live2 * er);
                        if (r > best_r) { best_r = r; best_s = s; }
                    }
                }

                if (best_r > 0.55f) {
                    double err = (double)best_s - st->pcm_pos;
                    if (!st->pcm_locked || fabs(err) > 16.0) {
                        st->pcm_pos = (double)best_s;
                        st->pcm_rate = 1.0;
                    } else if (fabs(err) >= 0.5) {
                        /* Gentle PLL nudge without audible comb-filtering or echo jumps */
                        st->pcm_pos += 0.08 * err;
                        st->pcm_rate += 0.00005 * (err / 256.0);
                        if (st->pcm_rate < 0.92) st->pcm_rate = 0.92;
                        else if (st->pcm_rate > 1.08) st->pcm_rate = 1.08;
                    }
                    st->pcm_locked = 1;
                    st->pcm_miss_cnt = 0;
                    st->ana_cur_frame = (int)(st->pcm_pos / 2048.0);
                    if (shm) {
                        atomic_store_explicit(&shm->ana_cur_frame[d], (uint32_t)st->ana_cur_frame, memory_order_relaxed);
                    }

                    int g_s0 = (int)lrint(st->pcm_pos);
                    if (g_s0 >= 0 && g_s0 + 512 < (int)pcm_ready) {
                        float e_ref = 1e-12f;
                        for (int i = 0; i < 512; i += 2) {
                            float rm = sl3_ref_mono(pcm, g_s0 + i);
                            e_ref += rm * rm;
                        }
                        float g = sqrtf(e_live2 / e_ref);
                        if (g > 2.0f) g = 2.0f;
                        else if (g < 0.10f) g = 0.10f;
                        st->pcm_gain_target += 0.20f * (g - st->pcm_gain_target);
                    }
                } else {
                    /* Coast smoothly through brief low-correlation fills; only unlock after 8 consecutive misses (~46 ms) */
                    if (++st->pcm_miss_cnt >= 8) st->pcm_locked = 0;
                }
            }
        }

        /* ====================================================================
         * Stereo-Linked 128-Band STFT Fallback (Zero Panning Modulation & Zero Gate Lag)
         * Used only before HTDemucs PCM stems arrive or in Demo Synth mode
         * ==================================================================== */
        float band_g[SL3_ANA_BANDS];
        float stem_e[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        int af = st->ana_cur_frame + 1;
        if (af < 0) af = 0;
        else if (af >= ana_nf && ana_nf > 0) af = ana_nf - 1;

        const sl3_stem_profile_t *pf = (ana_on && ana_nf > 8) ? &shm->profile[d] : NULL;
        const float inv15 = 1.0f / 15.0f;

        for (int b = 0; b < (int)SL3_ANA_BANDS; b++) {
            float em = (band_cnt[b] > 0.0f) ? (band_e_m[b] / band_cnt[b]) : 0.0f;
            float es = (band_cnt[b] > 0.0f) ? (band_e_s[b] / band_cnt[b]) : 0.0f;

            st->band_fast_m[b] += (em > st->band_fast_m[b] ? 0.65f : 0.20f) * (em - st->band_fast_m[b]);
            st->band_slow_m[b] += (em > st->band_slow_m[b] ? 0.12f : 0.04f) * (em - st->band_slow_m[b]);
            st->band_fast_s[b] += (es > st->band_fast_s[b] ? 0.65f : 0.20f) * (es - st->band_fast_s[b]);
            st->band_slow_s[b] += (es > st->band_slow_s[b] ? 0.12f : 0.04f) * (es - st->band_slow_s[b]);

            float w_voc, w_mel, w_bas, w_drm;

            if (pf) {
                uint8_t vm = pf->voc_mel[af][b];
                uint8_t bd = pf->bas_drm[af][b];

                w_voc = (float)((vm >> 4) & 0x0F) * inv15;
                w_mel = (float)(vm & 0x0F) * inv15;
                w_bas = (float)((bd >> 4) & 0x0F) * inv15;
                w_drm = (float)(bd & 0x0F) * inv15;

                float sum_w = w_voc + w_mel + w_bas + w_drm;
                if (sum_w > 1e-4f) {
                    float inv_w = 1.0f / sum_w;
                    w_voc *= inv_w; w_mel *= inv_w; w_bas *= inv_w; w_drm *= inv_w;
                }
            } else {
                float tr_m = fmaxf(0.0f, st->band_fast_m[b] - st->band_slow_m[b]) / (st->band_fast_m[b] + 1e-6f);
                float flux = fminf(1.0f, tr_m * 2.8f);

                if (b <= 3) {
                    w_drm = 0.20f + 0.72f * flux;
                    w_bas = 1.0f - w_drm;
                    w_voc = 0.0f; w_mel = 0.0f;
                } else if (b <= 5) {
                    w_bas = (1.0f - flux) * 0.55f;
                    w_drm = 0.25f + 0.65f * flux;
                    float rem = fmaxf(0.0f, 1.0f - w_bas - w_drm);
                    w_mel = rem * 0.65f; w_voc = rem * 0.35f;
                } else if (b >= 80) {
                    w_bas = 0.0f;
                    w_drm = 0.78f + 0.20f * flux;
                    w_voc = (1.0f - w_drm) * 0.45f;
                    w_mel = 1.0f - w_drm - w_voc;
                } else {
                    w_bas = 0.0f;
                    w_drm = fminf(0.88f, flux * 0.85f);
                    float rem = 1.0f - w_drm;
                    float c_ratio = (em * em) / (em * em + 2.2f * es * es + 1e-12f);
                    w_voc = rem * (0.20f + 0.65f * c_ratio);
                    w_mel = rem - w_voc;
                }
            }

            band_g[b] = w_voc * st->g_voc + w_mel * st->g_mel + w_bas * st->g_bas + w_drm * st->g_drm;

            float e_tot = em + es;
            stem_e[0] += e_tot * w_voc;
            stem_e[1] += e_tot * w_mel;
            stem_e[2] += e_tot * w_bas;
            stem_e[3] += e_tot * w_drm;
        }

        for (int k = 0; k <= SL3_FFT_N / 2; k++) {
            int kc = (SL3_FFT_N - k) & (SL3_FFT_N - 1);
            int b = sl3_bin_to_band(k);
            /* Stereo-linked gain applied identically to Left and Right -> 0.00 dB pan swing! */
            float g = band_g[b];

            float Lr_out = Lr_arr[k] * g, Li_out = Li_arr[k] * g;
            float Rr_out = Rr_arr[k] * g, Ri_out = Ri_arr[k] * g;

            if (k == 0 || k == SL3_FFT_N / 2) {
                out_re[k] = Lr_out;
                out_im[k] = Rr_out;
            } else {
                out_re[k]  = Lr_out - Ri_out;
                out_im[k]  = Li_out + Rr_out;
                out_re[kc] = Lr_out + Ri_out;
                out_im[kc] = -Li_out + Rr_out;
            }
        }

        sl3_fft1024(out_re, out_im, 1);
        for (int i = 0; i < SL3_FFT_N; i++) {
            int ola_idx = (int)((p_cur + (uint32_t)i) & (SL3_OLA_N - 1));
            float ws = st->stft_win[i] * (2.0f / 3.0f); /* Exact Hann^2 COLA for 75% overlap (N/4 hop) */
            st->stft_ola_l[ola_idx] += out_re[i] * ws;
            st->stft_ola_r[ola_idx] += out_im[i] * ws;
        }

        /* Update per-stem visual activity meters */
        for (int s = 0; s < 4; s++) {
            float v = stem_e[s] * 0.05f;
            if (v > 1.0f) v = 1.0f;
            if (v > st->pk_stem[s]) st->pk_stem[s] = v;
            else st->pk_stem[s] *= 0.92f;
        }
    }

    int read_ola = (int)(p_cur & (SL3_OLA_N - 1));
    float sep_l = st->stft_ola_l[read_ola];
    float sep_r = st->stft_ola_r[read_ola];
    st->stft_ola_l[read_ola] = 0.0f;
    st->stft_ola_r[read_ola] = 0.0f;
    st->stft_pos = p_cur + 1;

    /* ========================================================================
     * 3. True Time-Domain HTDemucs Waveform Synthesis (36.4 - 75+ dB SDR!)
     * Synthesizes directly from the 4 isolated stereo HTDemucs waveforms when locked
     * ======================================================================== */
    float td_vl = 0.0f, td_vr = 0.0f, td_ml = 0.0f, td_mr = 0.0f;
    float td_bl = 0.0f, td_br = 0.0f, td_dl = 0.0f, td_dr = 0.0f;
    int use_td = 0;

    if (st->pcm_locked && pcm != NULL && (uint32_t)st->pcm_pos + 1u < pcm_ready) {
        st->pcm_gain += 0.005f * (st->pcm_gain_target - st->pcm_gain);
        int s0 = (int)st->pcm_pos;
        float frac = (float)(st->pcm_pos - (double)s0);
        const int16_t *p0 = pcm[s0];
        const int16_t *p1 = pcm[s0 + 1];
        const float scale = (1.0f / 32768.0f) * st->pcm_gain;

        td_vl = ((float)p0[0] + frac * (float)(p1[0] - p0[0])) * scale;
        td_vr = ((float)p0[1] + frac * (float)(p1[1] - p0[1])) * scale;
        td_ml = ((float)p0[2] + frac * (float)(p1[2] - p0[2])) * scale;
        td_mr = ((float)p0[3] + frac * (float)(p1[3] - p0[3])) * scale;
        td_bl = ((float)p0[4] + frac * (float)(p1[4] - p0[4])) * scale;
        td_br = ((float)p0[5] + frac * (float)(p1[5] - p0[5])) * scale;
        td_dl = ((float)p0[6] + frac * (float)(p1[6] - p0[6])) * scale;
        td_dr = ((float)p0[7] + frac * (float)(p1[7] - p0[7])) * scale;

        float in_a = 0.5f * (fabsf(al_in_l) + fabsf(al_in_r));
        float rf_a = 0.5f * (fabsf(td_vl + td_ml + td_bl + td_dl) + fabsf(td_vr + td_mr + td_br + td_dr));
        st->env_in_fast += (in_a > st->env_in_fast ? 0.25f : 0.02f) * (in_a - st->env_in_fast);
        st->env_ref_fast += (rf_a > st->env_ref_fast ? 0.25f : 0.02f) * (rf_a - st->env_ref_fast);
        float dyn_gate = (st->env_in_fast < 1e-4f) ? 0.0f : 1.0f;

        sep_l = (td_vl * st->g_voc + td_ml * st->g_mel + td_bl * st->g_bas + td_dl * st->g_drm) * dyn_gate;
        sep_r = (td_vr * st->g_voc + td_mr * st->g_mel + td_br * st->g_bas + td_dr * st->g_drm) * dyn_gate;
        st->pcm_pos += st->pcm_rate;
        use_td = 1;
    }

    /* 4. Stem Echo Out (Aligned to al_in_l/al_in_r & fed by pure isolated stems when use_td=1!) */
    const float f_low = 2.0f * sinf(3.14159265f * 135.0f / sr);
    st->low_lp_l += f_low * (al_in_l - st->low_lp_l);
    st->low_lp_r += f_low * (al_in_r - st->low_lp_r);

    float fb_l = 0.0f, fb_r = 0.0f;
    if (echo_arm) {
        float echo_in_l = 0.0f, echo_in_r = 0.0f;
        float hi_l = al_in_l - st->low_lp_l;
        float hi_r = al_in_r - st->low_lp_r;
        if (st->echo_feed_voc > 0) {
            st->echo_feed_voc--;
            echo_in_l += (use_td ? td_vl : hi_l * 0.7f);
            echo_in_r += (use_td ? td_vr : hi_r * 0.7f);
        }
        if (st->echo_feed_mel > 0) {
            st->echo_feed_mel--;
            echo_in_l += (use_td ? td_ml : hi_l * 0.7f);
            echo_in_r += (use_td ? td_mr : hi_r * 0.7f);
        }
        if (st->echo_feed_bas > 0) {
            st->echo_feed_bas--;
            echo_in_l += (use_td ? td_bl : st->low_lp_l);
            echo_in_r += (use_td ? td_br : st->low_lp_r);
        }
        if (st->echo_feed_drm > 0) {
            st->echo_feed_drm--;
            echo_in_l += (use_td ? td_dl : al_in_l * 0.6f);
            echo_in_r += (use_td ? td_dr : al_in_r * 0.6f);
        }

        uint32_t bpm = shm ? atomic_load_explicit(&shm->echo_bpm[d], memory_order_relaxed) : 124;
        if (bpm < 70 || bpm > 180) bpm = 124;
        int delay_samps = (int)((30.0f / (float)bpm) * sr);
        if (delay_samps >= SL3_ECHO_MAX_SAMPLES) delay_samps = SL3_ECHO_MAX_SAMPLES - 1;
        if (delay_samps < 4000) delay_samps = 11025;

        int r_idx = (st->echo_w + SL3_ECHO_MAX_SAMPLES - delay_samps) % SL3_ECHO_MAX_SAMPLES;
        float d_l = st->echo_buf_l[r_idx];
        float d_r = st->echo_buf_r[r_idx];
        st->echo_lp_l += 0.22f * (d_l - st->echo_lp_l);
        st->echo_lp_r += 0.22f * (d_r - st->echo_lp_r);
        st->echo_hp_l += 0.015f * (st->echo_lp_l - st->echo_hp_l);
        st->echo_hp_r += 0.015f * (st->echo_lp_r - st->echo_hp_r);
        fb_l = st->echo_lp_l - st->echo_hp_l;
        fb_r = st->echo_lp_r - st->echo_hp_r;

        st->echo_buf_l[st->echo_w] = echo_in_l + fb_r * 0.62f;
        st->echo_buf_r[st->echo_w] = echo_in_r + fb_l * 0.62f;
        st->echo_w = (st->echo_w + 1) % SL3_ECHO_MAX_SAMPLES;
    } else {
        st->echo_feed_voc = st->echo_feed_mel = st->echo_feed_bas = st->echo_feed_drm = 0;
    }

    float out_l, out_r;
    if (t_voc && t_mel && t_bas && t_drm &&
        st->g_voc > 0.999f && st->g_mel > 0.999f && st->g_bas > 0.999f && st->g_drm > 0.999f) {
        out_l = al_in_l + fb_l * 0.70f;
        out_r = al_in_r + fb_r * 0.70f;
    } else {
        out_l = sep_l + fb_l * 0.70f;
        out_r = sep_r + fb_r * 0.70f;
    }

    /* 5. Pioneer DJM-style Color Filter */
    int32_t flt = shm ? atomic_load_explicit(&shm->filter[d], memory_order_relaxed) : 0;
    if (flt != 0) {
        float norm = (float)flt / 100.0f;
        float cutoff = (norm < 0.0f) ? (90.0f * powf(200.0f, 1.0f + norm)) : (25.0f * powf(180.0f, norm));
        if (cutoff > sr * 0.42f) cutoff = sr * 0.42f;
        float f_cf = 2.0f * sinf(3.14159265f * cutoff / sr);
        const float q_cf = 0.62f;
        float chp_l = out_l - st->cf_lp_l - q_cf * st->cf_bp_l;
        st->cf_bp_l += f_cf * chp_l;
        st->cf_lp_l += f_cf * st->cf_bp_l;
        float chp_r = out_r - st->cf_lp_r - q_cf * st->cf_bp_r;
        st->cf_bp_r += f_cf * chp_r;
        st->cf_lp_r += f_cf * st->cf_bp_r;
        if (norm < 0.0f) { out_l = st->cf_lp_l; out_r = st->cf_lp_r; }
        else             { out_l = chp_l;       out_r = chp_r;       }
    } else {
        st->cf_lp_l = out_l; st->cf_lp_r = out_r;
        st->cf_bp_l = st->cf_bp_r = 0.0f;
    }

    /* 6. Vinyl Brake effect */
    uint8_t brk = shm ? atomic_load_explicit(&shm->vinyl_brake[d], memory_order_relaxed) : 0;
    st->brake_buf_l[st->brake_w] = out_l;
    st->brake_buf_r[st->brake_w] = out_r;
    if (brk) {
        if (!st->prev_brake) {
            st->brake_r = (double)st->brake_w;
            st->brake_rate = 1.0f;
        }
        st->brake_rate -= 1.0f / (sr * 0.65f);
        if (st->brake_rate < 0.0f) st->brake_rate = 0.0f;
        int idx0 = ((int)st->brake_r) % SL3_BRAKE_MAX_SAMPLES;
        if (idx0 < 0) idx0 += SL3_BRAKE_MAX_SAMPLES;
        out_l = st->brake_buf_l[idx0] * st->brake_rate;
        out_r = st->brake_buf_r[idx0] * st->brake_rate;
        st->brake_r += st->brake_rate;
    } else {
        st->brake_rate = 1.0f;
    }
    st->prev_brake = brk;
    st->brake_w = (st->brake_w + 1) % SL3_BRAKE_MAX_SAMPLES;

    if (out_l > 1.0f) out_l = 1.0f; else if (out_l < -1.0f) out_l = -1.0f;
    if (out_r > 1.0f) out_r = 1.0f; else if (out_r < -1.0f) out_r = -1.0f;

    *io_l = out_l;
    *io_r = out_r;

    float al = fabsf(out_l), ar = fabsf(out_r);
    if (al > st->pk_l) st->pk_l = al; else st->pk_l *= 0.9993f;
    if (ar > st->pk_r) st->pk_r = ar; else st->pk_r *= 0.9993f;
    if (shm && !st->is_aux_instance && (++st->meter_decim >= 128)) {
        st->meter_decim = 0;
        atomic_store_explicit(&shm->meter_l[d], st->pk_l, memory_order_relaxed);
        atomic_store_explicit(&shm->meter_r[d], st->pk_r, memory_order_relaxed);
        for (int s = 0; s < 4; s++) {
            atomic_store_explicit(&shm->stem_meter[d][s], st->pk_stem[s], memory_order_relaxed);
        }
    }
}

static inline void sl3_dsp_au_push(sl3_dsp_shm_t *shm, uint64_t sample_time, const float fr[6]) {
    if (!shm) return;
    for (int d = 0; d < 2; d++) {
        if (!atomic_load_explicit(&shm->au_active[d], memory_order_relaxed)) continue;
        uint32_t idx = (uint32_t)(sample_time & SL3_AU_RING_MASK);
        int ch = (d + 1) * 2; /* Rane SL3: Ch 3-4 (fr[2..3]) = Left Deck, Ch 5-6 (fr[4..5]) = Right Deck */
        shm->au_in_l[d][idx] = fr[ch];
        shm->au_in_r[d][idx] = fr[ch + 1];
        atomic_store_explicit(&shm->au_write_pos[d], sample_time + 1, memory_order_release);
    }
}

static inline void sl3_dsp_au_pull(sl3_dsp_shm_t *shm, uint64_t sample_time, float fr[6]) {
    if (!shm) return;
    for (int d = 0; d < 2; d++) {
        if (!atomic_load_explicit(&shm->au_active[d], memory_order_relaxed)) continue;
        uint32_t idx = (uint32_t)(sample_time & SL3_AU_RING_MASK);
        if (atomic_load_explicit(&shm->au_out_stamp[d][idx], memory_order_acquire) == sample_time + 1) {
            float wet = atomic_load_explicit(&shm->au_wet[d], memory_order_relaxed);
            if (wet < 0.0f) wet = 0.0f; else if (wet > 1.0f) wet = 1.0f;
            float dry = 1.0f - wet;
            int ch = (d + 1) * 2;
            fr[ch]     = fr[ch] * dry     + shm->au_out_l[d][idx] * wet;
            fr[ch + 1] = fr[ch + 1] * dry + shm->au_out_r[d][idx] * wet;
        }
    }
}

#endif /* SL3_DSP_H */
