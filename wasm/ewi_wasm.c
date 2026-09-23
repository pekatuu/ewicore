// ewi_wasm.c — WASMバインディング。シングルインスタンス。
#include "../core/ewi_synth.h"
#include <emscripten.h>
#include <stdint.h>

static EwiSynth g_synth;
static float g_bufL[2048];
static float g_bufR[2048];

EMSCRIPTEN_KEEPALIVE void ewi_init(float sr) { Ewi_Init(&g_synth, sr); }
EMSCRIPTEN_KEEPALIVE void ewi_reset(void) { Ewi_Reset(&g_synth); }
EMSCRIPTEN_KEEPALIVE void ewi_noteOn(int n, int v) { Ewi_NoteOn(&g_synth, (uint8_t)n, (uint8_t)v); }
EMSCRIPTEN_KEEPALIVE void ewi_noteOff(int n) { Ewi_NoteOff(&g_synth, (uint8_t)n); }
EMSCRIPTEN_KEEPALIVE void ewi_cc(int cc, int v) { Ewi_CC(&g_synth, (uint8_t)cc, (uint8_t)v); }
EMSCRIPTEN_KEEPALIVE void ewi_pitchbend(int v) { Ewi_PitchBend(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_program(int p) { Ewi_SetPreset(&g_synth, (uint8_t)p); }
EMSCRIPTEN_KEEPALIVE void ewi_midi(int st, int d1, int d2) {
  Ewi_Midi(&g_synth, (uint8_t)st, (uint8_t)d1, (uint8_t)d2);
}
EMSCRIPTEN_KEEPALIVE void ewi_setBreath(float v) { Ewi_SetBreath01(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_setCutoff(float hz) { Ewi_SetCutoffBase(&g_synth, hz); }
EMSCRIPTEN_KEEPALIVE void ewi_setReso(float r) { Ewi_SetResonance(&g_synth, r); }
EMSCRIPTEN_KEEPALIVE void ewi_setBreathDepth(float hz) { Ewi_SetBreathDepth(&g_synth, hz); }
EMSCRIPTEN_KEEPALIVE void ewi_setFilterGamma(float g) { Ewi_SetFilterGamma(&g_synth, g); }
EMSCRIPTEN_KEEPALIVE void ewi_setGlide(float s) { Ewi_SetGlide(&g_synth, s); }
EMSCRIPTEN_KEEPALIVE void ewi_setFormant(float m) { Ewi_SetFormantMix(&g_synth, m); }
EMSCRIPTEN_KEEPALIVE void ewi_setDlyMix(float v) { Ewi_SetDelayMix(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_setDlyTime(float v) { Ewi_SetDelayTime(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_setDlyFb(float v) { Ewi_SetDelayFb(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_setRevMix(float v) { Ewi_SetRevMix(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE void ewi_setRevSize(float v) { Ewi_SetRevSize(&g_synth, v); }
EMSCRIPTEN_KEEPALIVE float ewi_getLevel(void) { return Ewi_GetLevel(&g_synth); }
EMSCRIPTEN_KEEPALIVE int ewi_quality(void) { return Ewi_Quality(); }
EMSCRIPTEN_KEEPALIVE float ewi_getCutoff(void) { return Ewi_GetCutoffHz(&g_synth); }

// frames<=2048で描画し、内部バッファに保持。JSはポインタで読む。
EMSCRIPTEN_KEEPALIVE void ewi_render(int frames) {
  if (frames < 1) return;
  if (frames > 2048) frames = 2048;
  Ewi_Render(&g_synth, g_bufL, g_bufR, frames);
}
EMSCRIPTEN_KEEPALIVE float* ewi_ptrL(void) { return g_bufL; }
EMSCRIPTEN_KEEPALIVE float* ewi_ptrR(void) { return g_bufR; }
