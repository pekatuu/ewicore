// ewi_synth.c — Pico2/PC/WASM共用。C99 + math.hのみ。
#include "ewi_synth.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const EwiPreset kPresets[EWI_NUM_PRESETS] = {
  // 0: Axis系リード (2Saw detune + 噛みつくLPF)
  {"Axis", EWI_WAVE_SAW, EWI_WAVE_SAW, 7.f, 0.5f, 0.5f,
   750.f, 6200.f, 0.38f, 2.0f, 0.35f, 1.6f, 0.06f, 0.95f,
   0.9f, 0.012f, 5.6f, 0.35f, 0.004f, 0.09f, 2.f,
   0.12f, 0.27f, 0.32f, 0.16f, 0.55f},
  // 1: Flute (sine寄り・フォルマント薄め)
  {"Flute", EWI_WAVE_SINE, EWI_WAVE_TRI, 4.f, 0.7f, 0.5f,
   900.f, 2800.f, 0.12f, 1.2f, 0.15f, 1.3f, 0.05f, 0.95f,
   0.8f, 0.008f, 5.0f, 0.25f, 0.006f, 0.12f, 2.f,
   0.10f, 0.30f, 0.30f, 0.20f, 0.50f},
  // 2: Brass (pulse混じり・レゾ高め)
  {"Brass", EWI_WAVE_SAW, EWI_WAVE_PULSE, -6.f, 0.45f, 0.35f,
   500.f, 5200.f, 0.30f, 2.4f, 0.45f, 1.8f, 0.08f, 0.95f,
   0.9f, 0.015f, 5.2f, 0.20f, 0.008f, 0.10f, 2.f,
   0.10f, 0.24f, 0.30f, 0.14f, 0.50f},
  // 3: SyncLead (BをSync風に明るく: 実装は激しいデチューンで近似)
  {"SyncLd", EWI_WAVE_SAW, EWI_WAVE_PULSE, 14.f, 0.5f, 0.22f,
   900.f, 7000.f, 0.45f, 2.8f, 0.25f, 1.5f, 0.05f, 0.95f,
   0.85f, 0.010f, 6.0f, 0.40f, 0.003f, 0.08f, 2.f,
   0.14f, 0.29f, 0.38f, 0.18f, 0.60f},
};

const EwiPreset* Ewi_GetPresetList(void) { return kPresets; }
int Ewi_Quality(void) { return EWI_QUALITY; }

#if EWI_QUALITY
// RBJバンドパス (constant 0dB peak gain)。係数はsr固定なのでInit時に計算。
static void formant_coeffs(float sr, float f0, float Q,
                           float* b0, float* b1, float* b2, float* a1, float* a2) {
  float w0 = 2.f * (float)M_PI * f0 / sr;
  float alpha = sinf(w0) / (2.f * Q);
  float cw = cosf(w0);
  float a0 = 1.f + alpha;
  *b0 = alpha / a0; *b1 = 0.f; *b2 = -alpha / a0;
  *a1 = -2.f * cw / a0; *a2 = (1.f - alpha) / a0;
}
static float biquad_tick(float in, float b0, float b1, float b2, float a1, float a2,
                         float* x1, float* x2, float* y1, float* y2) {
  float y = b0 * in + b1 * (*x1) + b2 * (*x2) - a1 * (*y1) - a2 * (*y2);
  *x2 = *x1; *x1 = in; *y2 = *y1; *y1 = y;
  return y;
}
#define SAT(x) tanhf(x)
#else
#define SAT(x) fast_tanh(x)
#endif

static float clampf(float x, float a, float b) {
  return x < a ? a : (x > b ? b : x);
}
// 高速tanh近似 (Pico版フィルタ内飽和用。HQ版はlibm tanhf使用)
#if !EWI_QUALITY
static float fast_tanh(float x) {
  float ax = x < 0 ? -x : x;
  return x * (27.f + ax) / (27.f + 9.f * ax);
}
#endif
// PolyBLEP
static float polyblep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.f;
  } else if (t > 1.f - dt) {
    t = (t - 1.f) / dt;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}
static float midi_to_freq(int n) {
  return 440.f * powf(2.f, ((float)n - 69.f) / 12.f);
}

void Ewi_Init(EwiSynth* s, float sampleRate) {
  // ゼロクリア (memsetを使わず移植性確保)
  for (unsigned i = 0; i < sizeof(*s); i++) ((uint8_t*)s)[i] = 0;
  s->sr = sampleRate > 8000.f ? sampleRate : 48000.f;
  s->note = -1;
  s->volume = 1.f;
  s->expression = 1.f;
  s->pitchbend = 8192;
  s->channel = 0;
  Ewi_SetPreset(s, 0);
#if EWI_QUALITY
  formant_coeffs(s->sr, 850.f, 4.5f,
                 &s->f1_b0, &s->f1_b1, &s->f1_b2, &s->f1_a1, &s->f1_a2);
  formant_coeffs(s->sr, 2300.f, 6.0f,
                 &s->f2_b0, &s->f2_b1, &s->f2_b2, &s->f2_a1, &s->f2_a2);
#endif
  Ewi_Reset(s);
}

void Ewi_Reset(EwiSynth* s) {
  s->phaseA = s->phaseB = 0.f;
  s->freq_sm = 440.f;
  s->cutoff_sm = s->pr.cutoff_base;
  s->breath_env = 0.f;
  s->lfo_phase = 0.f;
  s->lad1 = s->lad2 = s->lad3 = s->lad4 = 0.f;
  s->lad_y4d = 0.f;
  s->fbp1_z1 = s->fbp1_z2 = s->fbp2_z1 = s->fbp2_z2 = 0.f;
#if EWI_QUALITY
  s->os_prev = 0.f;
  s->f1_x1 = s->f1_x2 = s->f1_y1 = s->f1_y2 = 0.f;
  s->f2_x1 = s->f2_x2 = s->f2_y1 = s->f2_y2 = 0.f;
#endif
  // 空間系: リバーブ長はsr依存 (48k超は48k換算に丸め)
  {
    static const int combBase[EWI_REV_NCOMB] = {1116,1188,1277,1356,1422,1491,1557,1617};
    static const int apBase[EWI_REV_NAP] = {556,441,341,225};
    float sref = s->sr > 48000.f ? 48000.f : s->sr;
    float sc = sref / 44100.f;
    for (int i = 0; i < EWI_REV_NCOMB; i++) {
      int L = (int)(combBase[i] * sc) + 2;
      if (L > EWI_REV_COMB_MAX) L = EWI_REV_COMB_MAX;
      s->rev_comb_len[i] = L;
      s->rev_comb_idx[i] = 0;
      s->rev_comb_fs[i] = 0.f;
      for (int j = 0; j < L; j++) s->rev_comb[i][j] = 0.f;
    }
    for (int i = 0; i < EWI_REV_NAP; i++) {
      int L = (int)(apBase[i] * sc) + 2;
      if (L > EWI_REV_AP_MAX) L = EWI_REV_AP_MAX;
      s->rev_ap_len[i] = L;
      s->rev_ap_idx[i] = 0;
      for (int j = 0; j < L; j++) s->rev_ap[i][j] = 0.f;
    }
  }
  for (int i = 0; i < EWI_DLY_MAX; i++) s->dly_buf[i] = 0.f;
  for (int i = 0; i < EWI_DEC_MAX; i++) s->dec_buf[i] = 0.f;
  s->dly_pos = 0;
  s->dly_tap_sm = clampf(s->pr.dly_time, 0.05f, 0.5f) * s->sr;
  s->dly_fb_sm = s->pr.dly_fb;
  s->dly_mix_sm = s->pr.dly_mix;
  s->dly_loop_lp = 0.f;
  s->rev_mix_sm = s->pr.rev_mix;
  s->rev_size_sm = s->pr.rev_size;
  s->dec_pos = 0;
  s->level = 0.f;
  s->vib_depth_sm = 0.f;
}

void Ewi_SetPreset(EwiSynth* s, uint8_t no) {
  if (no >= EWI_NUM_PRESETS) no = 0;
  s->preset_no = no;
  s->pr = kPresets[no];
}

// ---- MIDI ----
void Ewi_NoteOn(EwiSynth* s, uint8_t note, uint8_t vel) {
  (void)vel; // EWIはベロシティよりブレスで強弱。velは軽く無視(将来拡張可)
  if (note > 127) return;
  s->last_note = note;
  // レガート: 既発音中は周波数だけ滑らかに遷移(リトリガなし)
  if (s->note < 0) {
    s->note = note;
    // 初発音はグライドなしで即追従(タンギングのキレ確保)
    if (!s->porta_on) s->freq_sm = midi_to_freq(note);
  } else {
    s->note = note;
  }
}
void Ewi_NoteOff(EwiSynth* s, uint8_t note) {
  if (s->note == (int)note) s->note = -1;
}
void Ewi_CC(EwiSynth* s, uint8_t cc, uint8_t val) {
  float v = clampf(val / 127.f, 0.f, 1.f);
  switch (cc) {
    case 2:  s->breath_raw = v; break;              // Breath (EWI5000標準)
    case 102: s->breath_raw = v; break;             // Breath代替 (EWI設定でCC102を使う場合)
    case 1:  s->cc1 = v; break;                     // Mod / Vibrato depth
    case 7:  s->volume = v; break;
    case 11: s->expression = v; break;
    case 5:  s->porta_time = v * 0.4f; break;       // Porta/Glide time (CC65なしでも有効)
    case 65: s->porta_on = (val >= 64); break;      // Porta on/off
    case 120: case 123: // AllSoundOff / AllNotesOff
      s->note = -1; s->breath_raw = 0; break;
    default: break;
  }
}
void Ewi_PitchBend(EwiSynth* s, int v14) {
  if (v14 < 0) v14 = 0; if (v14 > 16383) v14 = 16383;
  s->pitchbend = v14;
}
void Ewi_SetBreath01(EwiSynth* s, float v) { s->breath_raw = clampf(v, 0.f, 1.f); }

void Ewi_Midi(EwiSynth* s, uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t type = status & 0xF0;
  uint8_t ch = status & 0x0F;
  if (ch != (s->channel & 0x0F)) return; // ch一致のみ(必要ならomni拡張)
  switch (type) {
    case 0x90: if (d2 == 0) Ewi_NoteOff(s, d1); else Ewi_NoteOn(s, d1, d2); break;
    case 0x80: Ewi_NoteOff(s, d1); break;
    case 0xB0: Ewi_CC(s, d1, d2); break;
    case 0xE0: Ewi_PitchBend(s, (int)d2 * 128 + (int)d1); break;
    case 0xC0: Ewi_SetPreset(s, d1 % EWI_NUM_PRESETS); break;
    default: break;
  }
}

void Ewi_SetCutoffBase(EwiSynth* s, float hz) { s->pr.cutoff_base = clampf(hz, 80.f, 12000.f); }
void Ewi_SetResonance(EwiSynth* s, float r)   { s->pr.resonance = clampf(r, 0.f, 0.95f); }
void Ewi_SetBreathDepth(EwiSynth* s, float hz){ s->pr.cutoff_breath = clampf(hz, 0.f, 12000.f); }
void Ewi_SetGlide(EwiSynth* s, float sec)     { s->pr.glide_s = clampf(sec, 0.f, 0.5f); }
void Ewi_SetFormantMix(EwiSynth* s, float m)  { s->pr.formant_mix = clampf(m, 0.f, 1.f); }
void Ewi_SetDelayMix(EwiSynth* s, float m)   { s->pr.dly_mix = clampf(m, 0.f, 0.6f); }
void Ewi_SetDelayTime(EwiSynth* s, float sec){ s->pr.dly_time = clampf(sec, 0.05f, 0.5f); }
void Ewi_SetDelayFb(EwiSynth* s, float fb)   { s->pr.dly_fb = clampf(fb, 0.f, 0.7f); }
void Ewi_SetRevMix(EwiSynth* s, float m)     { s->pr.rev_mix = clampf(m, 0.f, 0.6f); }
void Ewi_SetRevSize(EwiSynth* s, float size) { s->pr.rev_size = clampf(size, 0.f, 1.f); }

// 固定フォルマントはラダー段差分による軽量近似(下記参照)。RBJ再計算不要でPico/WASM共通。

#if EWI_QUALITY
// HQ: ラダー1サブステップ (2xOSで2回呼ぶ)
static void ladder_sub(EwiSynth* s, float g, float k, float x) {
  float u = SAT(x - k * s->lad_y4d);
  s->lad1 += g * (u - s->lad1);
  s->lad2 += g * (SAT(s->lad1) - s->lad2);
  s->lad3 += g * (SAT(s->lad2) - s->lad3);
  s->lad4 += g * (SAT(s->lad3) - s->lad4);
  s->lad_y4d = s->lad4;
}
#endif

// --- 空間系ヘルパー (Delay + Freeverb系。Pico/PC共通) ---
static float rev_comb_tick(float in, float* buf, int len, int* idx, float* fs, float fb) {
  int i = *idx;
  float y = buf[i];
  *fs = y * 0.55f + (*fs) * 0.45f; // ループ内ダンピング
  buf[i] = in + (*fs) * fb;
  *idx = (i + 1 >= len) ? 0 : i + 1;
  return y;
}
static float rev_ap_tick(float in, float* buf, int len, int* idx) {
  int i = *idx;
  float y = buf[i];
  buf[i] = in + y * 0.5f;
  *idx = (i + 1 >= len) ? 0 : i + 1;
  return y - in;
}

void Ewi_Render(EwiSynth* s, float* outL, float* outR, int frames) {  const float sr = s->sr;
  const float dt_inv = 1.f / sr;
  float vib_target = s->cc1 * s->pr.vibrato_max; // semitone
  // CC5が来ていればプリセットより優先 (EWIグライド操作用。CC5=0でプリセットに戻る)
  float glide_t = s->porta_time > 0.001f ? s->porta_time : s->pr.glide_s;
  // glide係数 (指数平滑)。0なら即時。
  float gfreq = glide_t <= 0.0005f ? 1.f : 1.f - expf(-2.2f * dt_inv * 1000.f / (glide_t * 1000.f + 2.f));
  if (glide_t <= 0.0005f) gfreq = 1.f;
  float gcut = 1.f - expf(-2.f * M_PI * 28.f * dt_inv);   // cutoff平滑 ~28Hz
  float gbr_a = 1.f - expf(-2.f * M_PI * (1.f / (s->pr.env_attack + 0.001f)) * dt_inv * 0.16f);
  float gbr_r = 1.f - expf(-2.f * M_PI * (1.f / (s->pr.env_release + 0.005f)) * dt_inv * 0.16f);
  float glfo = 2.f * (float)M_PI * s->pr.vibrato_rate * dt_inv;
  float gvib = 1.f - expf(-2.f * M_PI * 8.f * dt_inv);
  float gfx = 1.f - expf(-2.f * M_PI * 15.f * dt_inv);  // 空間系ミックス平滑
  float gtap = 1.f - expf(-2.f * M_PI * 8.f * dt_inv);  // ディレイタイム平滑

  for (int i = 0; i < frames; i++) {
    // --- ブレス整形 ---
    float b = s->breath_raw;
    float th = s->pr.breath_thresh, mx = s->pr.breath_max;
    float bn = (b - th) / (mx - th);
    bn = clampf(bn, 0.f, 1.f);
    float bshaped = powf(bn, s->pr.breath_curve);
    if (s->note < 0) bshaped = 0.f; // 音符なしでは鳴らさない(EWI式)
    float gbr = (bshaped > s->breath_env) ? gbr_a : gbr_r;
    s->breath_env += (bshaped - s->breath_env) * gbr;

    // --- ピッチ ---
    s->vib_depth_sm += (vib_target - s->vib_depth_sm) * gvib;
    s->lfo_phase += glfo;
    if (s->lfo_phase > 2.f * (float)M_PI) s->lfo_phase -= 2.f * (float)M_PI;
    float vib_semi = sinf(s->lfo_phase) * s->vib_depth_sm;
    float bend_semi = ((float)s->pitchbend - 8192.f) / 8192.f * s->pr.bend_range;
    float freq_t = s->note >= 0 ? midi_to_freq(s->note) : s->freq_sm;
    freq_t *= powf(2.f, (bend_semi + vib_semi) / 12.f);
    s->freq_sm += (freq_t - s->freq_sm) * (s->note >= 0 && !s->porta_on && s->breath_env < 0.02f ? 1.f : gfreq);
    float f = clampf(s->freq_sm, 20.f, 6000.f);

    // detune B
    float fA = f;
    float fB = f * powf(2.f, s->pr.detune_cents / 1200.f);
    float dtA = fA * dt_inv, dtB = fB * dt_inv;

    // --- OSC (PolyBLEP) ---
    float phA = s->phaseA, phB = s->phaseB;
    float oscA = 0.f, oscB = 0.f;
    // A
    if (s->pr.waveA == EWI_WAVE_SAW) {
      float n = 2.f * phA - 1.f;
      oscA = n - polyblep(phA, dtA);
    } else if (s->pr.waveA == EWI_WAVE_PULSE) {
      float pw = 0.5f;
      float n = phA < pw ? 1.f : -1.f;
      oscA = n + polyblep(phA, dtA) - polyblep(fmodf(phA + 1.f - pw, 1.f), dtA);
    } else if (s->pr.waveA == EWI_WAVE_TRI) {
      oscA = 4.f * (phA < 0.5f ? phA : 1.f - phA) - 1.f;
    } else {
      oscA = sinf(2.f * (float)M_PI * phA);
    }
    // B
    if (s->pr.waveB == EWI_WAVE_SAW) {
      float n = 2.f * phB - 1.f;
      oscB = n - polyblep(phB, dtB);
    } else if (s->pr.waveB == EWI_WAVE_PULSE) {
      float pw = clampf(s->pr.pulseWidth, 0.05f, 0.95f);
      float n = phB < pw ? 1.f : -1.f;
      oscB = n + polyblep(phB, dtB) - polyblep(fmodf(phB + 1.f - pw, 1.f), dtB);
    } else if (s->pr.waveB == EWI_WAVE_TRI) {
      oscB = 4.f * (phB < 0.5f ? phB : 1.f - phB) - 1.f;
    } else {
      oscB = sinf(2.f * (float)M_PI * phB);
    }
    s->phaseA = phA + dtA >= 1.f ? phA + dtA - 1.f : phA + dtA;
    s->phaseB = phB + dtB >= 1.f ? phB + dtB - 1.f : phB + dtB;

    float mix = clampf(s->pr.mixAB, 0.f, 1.f);
    float osc = oscA * (1.f - mix) + oscB * mix;
    osc *= 0.5f; // ヘッドルーム

    // --- カットオフ (ブレス連動・オーディオレート平滑) ---
    float env = s->breath_env;
    float fc_t = s->pr.cutoff_base + s->pr.cutoff_breath * env;
    fc_t = clampf(fc_t, 60.f, 16000.f);
    s->cutoff_sm += (fc_t - s->cutoff_sm) * gcut;
    float fc = s->cutoff_sm;

    // --- ラダー (4段カスケード + feedback + 飽和) ---
    float wc = 2.f * (float)M_PI * fc * dt_inv;
    float k = s->pr.resonance * 3.8f;
    float in = osc * s->pr.drive;
#if EWI_QUALITY
    // HQ: 2xオーバーサンプリング (線形補間の中点+端点で2サブステップ)
    float g2 = (wc * 0.5f) / (1.f + wc * 0.5f);
    if (g2 > 0.9f) g2 = 0.9f; if (g2 < 0.001f) g2 = 0.001f;
    ladder_sub(s, g2, k, (s->os_prev + in) * 0.5f);
    ladder_sub(s, g2, k, in);
    s->os_prev = in;
    float lp = s->lad4;

    // --- フォルマント (RBJバンドパス2並列) ---
    float bp1 = biquad_tick(lp, s->f1_b0, s->f1_b1, s->f1_b2, s->f1_a1, s->f1_a2,
                            &s->f1_x1, &s->f1_x2, &s->f1_y1, &s->f1_y2);
    float bp2 = biquad_tick(lp, s->f2_b0, s->f2_b1, s->f2_b2, s->f2_a1, s->f2_a2,
                            &s->f2_x1, &s->f2_x2, &s->f2_y1, &s->f2_y2);
    float form = bp1 * 0.6f + bp2 * 0.4f;
    float y = lp * (1.f - s->pr.formant_mix * 0.5f) + form * s->pr.formant_mix * 0.5f;
#else
    float g = wc / (1.f + wc); // 安定な近似
    if (g > 0.9f) g = 0.9f; if (g < 0.001f) g = 0.001f;
    float u = in - k * s->lad_y4d;
    u = SAT(u);
    s->lad1 += g * (u - s->lad1);
    s->lad2 += g * (SAT(s->lad1) - s->lad2);
    s->lad3 += g * (SAT(s->lad2) - s->lad3);
    s->lad4 += g * (SAT(s->lad3) - s->lad4);
    s->lad_y4d = s->lad4;
    float lp = s->lad4;

    // --- フォルマント (簡易2BPF並列: ラダー段差分による軽量近似) ---
    float bp1 = (s->lad3 - s->lad4) * 2.f;      // 高域抜き出し近似1
    float bp2 = (s->lad2 - s->lad3) * 1.5f;     // 近似2
    float form = (bp1 * 0.6f + bp2 * 0.4f);
    float y = lp * (1.f - s->pr.formant_mix * 0.5f) + form * s->pr.formant_mix * 0.5f;
#endif

    // --- VCA (ブレス包絡) ---
    float gain = env * env; // 立ち上がりを管楽器的に
    gain *= s->pr.vca_gain * s->volume * (0.3f + 0.7f * s->expression);
    float mono = y * gain;
    mono = SAT(mono * 1.2f) * 0.8f;

    // --- 空間系: ディレイ + リバーブ ---
    s->dly_mix_sm += (s->pr.dly_mix - s->dly_mix_sm) * gfx;
    s->dly_fb_sm += (s->pr.dly_fb - s->dly_fb_sm) * gfx;
    s->rev_mix_sm += (s->pr.rev_mix - s->rev_mix_sm) * gfx;
    s->rev_size_sm += (s->pr.rev_size - s->rev_size_sm) * gfx;
    float tapTgt = clampf(s->pr.dly_time, 0.05f, 0.5f) * sr;
    if (tapTgt > (float)(EWI_DLY_MAX - 2)) tapTgt = (float)(EWI_DLY_MAX - 2);
    s->dly_tap_sm += (tapTgt - s->dly_tap_sm) * gtap;
    // 分数タップ読出し (線形補間。タイム変更時のクリック防止)
    float rp = (float)s->dly_pos - s->dly_tap_sm;
    while (rp < 0.f) rp += (float)EWI_DLY_MAX;
    int ri0 = (int)rp;
    float rfrac = rp - (float)ri0;
    int ri1 = (ri0 + 1 >= EWI_DLY_MAX) ? 0 : ri0 + 1;
    float tap = s->dly_buf[ri0] * (1.f - rfrac) + s->dly_buf[ri1] * rfrac;
    s->dly_loop_lp += (tap - s->dly_loop_lp) * 0.3f; // ループ内ダンピング
    s->dly_buf[s->dly_pos] = mono + s->dly_loop_lp * s->dly_fb_sm;
    s->dly_pos = (s->dly_pos + 1 >= EWI_DLY_MAX) ? 0 : s->dly_pos + 1;
    // Freeverb系モノリバーブ (8コム並列→4オールパス直列)
    float rfb = 0.70f + 0.26f * s->rev_size_sm;
    float rsum = 0.f;
    for (int c = 0; c < EWI_REV_NCOMB; c++) {
      rsum += rev_comb_tick(mono * 0.25f, s->rev_comb[c], s->rev_comb_len[c],
                            &s->rev_comb_idx[c], &s->rev_comb_fs[c], rfb);
    }
    float rverb = rsum * 0.125f;
    for (int a = 0; a < EWI_REV_NAP; a++) {
      rverb = rev_ap_tick(rverb, s->rev_ap[a], s->rev_ap_len[a], &s->rev_ap_idx[a]);
    }
    // Rchは18ms遅延で拡散 (ステレオ幅)
    int decLen = (int)(0.018f * sr);
    if (decLen > EWI_DEC_MAX - 1) decLen = EWI_DEC_MAX - 1;
    int dwpos = s->dec_pos;
    float verbR = s->dec_buf[(dwpos - decLen + EWI_DEC_MAX * 2) % EWI_DEC_MAX];
    s->dec_buf[dwpos] = rverb;
    s->dec_pos = (dwpos + 1 >= EWI_DEC_MAX) ? 0 : dwpos + 1;

    float wetD = tap * s->dly_mix_sm;
    float wetL = rverb * s->rev_mix_sm;
    float wetR = verbR * s->rev_mix_sm;
    float oL = mono + wetD + wetL;
    float oR = mono + wetD + wetR;
    // 安全リミッタ (ウェット加算時のクリップ防止)
    oL = clampf(oL, -1.f, 1.f);
    oR = clampf(oR, -1.f, 1.f);

    s->level += (fabsf(mono) - s->level) * 0.05f;
    outL[i] = oL;
    outR[i] = oR;
  }
}

void Ewi_RenderS16Mono(EwiSynth* s, int16_t* out, int frames) {
  // Pico用: スタック消費を抑えるため小ブロックで処理
  float bl[64], br[64];
  int pos = 0;
  while (pos < frames) {
    int n = frames - pos > 64 ? 64 : frames - pos;
    Ewi_Render(s, bl, br, n);
    for (int i = 0; i < n; i++) {
      float v = clampf(bl[i], -1.f, 1.f);
      out[pos + i] = (int16_t)(v * 32767.f);
    }
    pos += n;
  }
}

float Ewi_GetLevel(const EwiSynth* s) { return s->level; }
float Ewi_GetCutoffHz(const EwiSynth* s) { return s->cutoff_sm; }
