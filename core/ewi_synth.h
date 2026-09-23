// ewi_synth.h — Pico2 / PC / WASM 共用シンセコア
// EWI5000 (MIDI前提) 向けモノフォニック VA音源。AXIS系リードを基準にした設計。
// 依存: C99 + math.h のみ。mallocなし。Pico SDK不要。
//
// 音質モード (条件コンパイル):
//   -DEWI_QUALITY=0 (既定): Pico2向け軽量版。等倍ラダー近似+高速tanh近似+簡易フォルマント
//   -DEWI_QUALITY=1       : PC向け高品位版。ラダー2xオーバーサンプリング+libm tanh+RBJフォルマント
// Pico用ファームは0、PC/WASM高品位版は1でビルドする。WASMでは両方を別モジュールとして
// ビルドし、Webアプリ上でA/B比較できる (wasm/build_wasm.ps1 参照)。
#pragma once
#include <stdint.h>

#ifndef EWI_QUALITY
#define EWI_QUALITY 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define EWI_WAVE_SAW   0
#define EWI_WAVE_PULSE 1
#define EWI_WAVE_TRI   2
#define EWI_WAVE_SINE  3

#define EWI_NUM_PRESETS 4

// 空間系バッファ上限 (malloc禁止のため構造体内に静的確保。合計約165KB。
// EwiSynthはstatic/global配置すること。スタック確保禁止)
#define EWI_DLY_MAX   24000  // ディレイ最大 0.5s@48kHz
#define EWI_REV_COMB_MAX 1776 // リバーブコム最大長 (1617*48000/44100+余裕)
#define EWI_REV_AP_MAX   624  // リバーブオールパス最大長
#define EWI_REV_NCOMB 8
#define EWI_REV_NAP   4
#define EWI_DEC_MAX   1024   // ステレオ拡散用短ディレイ

typedef struct {
  char  name[16];
  uint8_t waveA;
  uint8_t waveB;
  float detune_cents;   // Bのデチューン(+/- cent)
  float mixAB;          // 0=Aのみ 1=Bのみ (0.5=等)
  float pulseWidth;     // PULSE用 0.05..0.95
  float cutoff_base;    // Hz (ブレス0の時)
  float cutoff_breath;  // Hz (ブレス127で上乗せ)
  float resonance;      // 0..0.95
  float drive;          // 1..4 フィルタ前ドライブ
  float formant_mix;    // 0..1 固定フォルマントブレンド
  float breath_curve;   // 0.5..3.0 指数カーブ
  float breath_thresh;  // 0..0.3 鳴り始め閾値
  float breath_max;     // 0.5..1.0 フルに達する位置
  float vca_gain;       // 0..1.5 出力ゲイン
  float glide_s;        // 0..0.5 ポルタメント
  float vibrato_rate;   // Hz
  float vibrato_max;    // semitone (CC1=127時の深さ)
  float env_attack;     // s ブレスアタック平滑
  float env_release;    // s ブレスリリース平滑
  float bend_range;     // semitone (PitchBend +/-)
  float dly_mix;        // 0..0.6 ディレイ混合
  float dly_time;       // s 0.05..0.5 ディレイタイム
  float dly_fb;         // 0..0.7 ディレイフィードバック
  float rev_mix;        // 0..0.6 リバーブ混合
  float rev_size;       // 0..1 リバーブ部屋サイズ
  float filter_gamma;   // 0.3..3.0 ブレス→カットオフの冪 (1.0=従来の線形)
} EwiPreset;

typedef struct {
  float   sr;
  EwiPreset pr;
  uint8_t preset_no;

  // MIDI状態
  int     note;          // -1=無音
  int     last_note;
  float   breath_raw;    // 0..1 (CC2)
  float   cc1;           // 0..1 vibrato depth
  float   volume;        // 0..1 (CC7)
  float   expression;    // 0..1 (CC11)
  int     pitchbend;     // 0..16383 center 8192
  int     porta_on;
  float   porta_time;    // s (CC5)
  uint8_t channel;       // 受信ch 0..15 (16=omni扱いは使わない)

  // 内部DSP状態
  float phaseA, phaseB;
  float freq_sm;         // glide後周波数
  float cutoff_sm;       // 平滑後カットオフ
  float breath_env;      // 平滑後ブレス包絡
  float lfo_phase;
  float lad1, lad2, lad3, lad4; // ラダー段状態
  float lad_y4d;         // フィードバック用前回出力
  float fbp1_z1, fbp1_z2; // フォルマントBPF1
  float fbp2_z1, fbp2_z2; // フォルマントBPF2
#if EWI_QUALITY
  float os_prev;         // HQ: 2xOS用前回入力
  float f1_b0, f1_b1, f1_b2, f1_a1, f1_a2; // HQ: RBJフォルマント係数
  float f1_x1, f1_x2, f1_y1, f1_y2;        // HQ: RBJ状態
  float f2_b0, f2_b1, f2_b2, f2_a1, f2_a2;
  float f2_x1, f2_x2, f2_y1, f2_y2;
#endif
  // --- 空間系 (Delay + Freeverb系Reverb。Pico/PC共通) ---
  float dly_buf[EWI_DLY_MAX];
  int   dly_pos;
  float dly_tap_sm;      // 分数タップ平滑 (クリック防止)
  float dly_fb_sm, dly_mix_sm;
  float dly_loop_lp;     // ループ内ダンピング状態
  float rev_comb[EWI_REV_NCOMB][EWI_REV_COMB_MAX];
  int   rev_comb_len[EWI_REV_NCOMB];
  int   rev_comb_idx[EWI_REV_NCOMB];
  float rev_comb_fs[EWI_REV_NCOMB]; // ダンピング用状態
  float rev_ap[EWI_REV_NAP][EWI_REV_AP_MAX];
  int   rev_ap_len[EWI_REV_NAP];
  int   rev_ap_idx[EWI_REV_NAP];
  float rev_mix_sm, rev_size_sm;
  float dec_buf[EWI_DEC_MAX]; // Rch拡散用短ディレイ
  int   dec_pos;
  float level;           // メータ用ピーク
  float vib_depth_sm;
} EwiSynth;

void  Ewi_Init(EwiSynth* s, float sampleRate);
void  Ewi_Reset(EwiSynth* s);
// ビルド時音質モードを返す (0=Pico軽量 / 1=PC高品位)
int   Ewi_Quality(void);
void  Ewi_SetPreset(EwiSynth* s, uint8_t no);
const EwiPreset* Ewi_GetPresetList(void);

// MIDI入力 (生ステータス対応。PicoのUART受信→そのまま渡せる)
void Ewi_Midi(EwiSynth* s, uint8_t status, uint8_t d1, uint8_t d2);
void Ewi_NoteOn(EwiSynth* s, uint8_t note, uint8_t vel);
void Ewi_NoteOff(EwiSynth* s, uint8_t note);
void Ewi_CC(EwiSynth* s, uint8_t cc, uint8_t val);
void Ewi_PitchBend(EwiSynth* s, int v14); // 0..16383
void Ewi_SetBreath01(EwiSynth* s, float v); // 自作ブレスセンサ用ADC直結

// Web/Pico UI用パラメータ上書き (プリセット基準の微調整)
void Ewi_SetCutoffBase(EwiSynth* s, float hz);
void Ewi_SetResonance(EwiSynth* s, float r);
void Ewi_SetBreathDepth(EwiSynth* s, float hz);
void Ewi_SetFilterGamma(EwiSynth* s, float g); // 0.3..3.0
void Ewi_SetGlide(EwiSynth* s, float sec);
void Ewi_SetFormantMix(EwiSynth* s, float m);
// 空間系
void Ewi_SetDelayMix(EwiSynth* s, float m);   // 0..0.6
void Ewi_SetDelayTime(EwiSynth* s, float sec);// 0.05..0.5
void Ewi_SetDelayFb(EwiSynth* s, float fb);   // 0..0.7
void Ewi_SetRevMix(EwiSynth* s, float m);     // 0..0.6
void Ewi_SetRevSize(EwiSynth* s, float size); // 0..1

// オーディオ描画。L/Rステレオ(同相+軽い幅付け)。戻り値なし。
void Ewi_Render(EwiSynth* s, float* outL, float* outR, int frames);
// Pico I2S用の簡易版: mono int16詰め
void Ewi_RenderS16Mono(EwiSynth* s, int16_t* out, int frames);

float Ewi_GetLevel(const EwiSynth* s);
float Ewi_GetCutoffHz(const EwiSynth* s);

#ifdef __cplusplus
}
#endif
