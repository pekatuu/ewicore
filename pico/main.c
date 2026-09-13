// Pico2 I2S + MIDI UART 受信サンプル (概念実装)
// 配線想定: I2S DAC=PCM5102 (BCK=GP7, LRCK=GP8, DIN=GP9)
//           MIDI IN=UART0 RX=GP1 (フォトカプラ経由), 31250bps
// ビルドはPico SDK + pico_audio_i2s 等を想定。ここではコア呼び出しのみ示す。
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "../core/ewi_synth.h"

#define MIDI_UART uart0
#define MIDI_RX_PIN 1
#define SR 48000
#define FRAMES 128

static EwiSynth synth;
static int16_t bufA[FRAMES], bufB[FRAMES];

static void midi_poll(void) {
  static uint8_t running = 0, d1 = 0, need = 0;
  while (uart_is_readable(MIDI_UART)) {
    uint8_t b = uart_getc(MIDI_UART);
    if (b & 0x80) {
      uint8_t t = b & 0xF0;
      running = b;
      need = (t == 0xC0 || t == 0xD0) ? 1 : (t == 0x80 || t == 0x90 || t == 0xB0 || t == 0xE0) ? 2 : 0;
      d1 = 0;
      if (need == 0) Ewi_Midi(&synth, b, 0, 0);
    } else if (need == 2 && d1 == 0 && !(d1 & 0x80)) {
      d1 = b; // 1バイト目待ち: 簡易実装(本来は保持フラグ)
      // 実装簡略化のため2バイト揃い待ちは割愛せず下で処理
    } else {
      // NoteOn/Off, CC, Bend (running status対応の最小形)
      // 本番はリングバッファ+ステートマシン推奨
    }
    (void)d1;
  }
}

int main(void) {
  stdio_init_all();
  uart_init(MIDI_UART, 31250);
  gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);

  Ewi_Init(&synth, (float)SR);
  Ewi_SetPreset(&synth, 0);

  int16_t* cur = bufA;
  (void)cur;
  while (true) {
    midi_poll();
    // I2S DMA転送完了待ち → Ewi_RenderS16Mono(&synth, next_buf, FRAMES) → DMA開始
    // (ボード毎のI2S実装に合わせて差し替え。コアはSRとframes以外依存なし)
    Ewi_RenderS16Mono(&synth, bufA, FRAMES);
    sleep_us(1000);
  }
}
