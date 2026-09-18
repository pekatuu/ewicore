# EWI5000用 VAシンセコア (Pico2 / PC / WASM共用)

EWI5000 (MIDI前提) 向けモノフォニック・バーチャルアナログ音源。
EWI3020m `Axis` 系リードを基準に、2VCO + 共振LPF + 固定フォルマント + ブレス連動VCAを
Pico2 / PC / Web (WASM) で同一コード (`core/ewi_synth.c`) から動作させる。

```
ewi-synth/
  core/       ewi_synth.h / ewi_synth.c   … 共通コア(C99, mallocなし, Pico SDK不要)
  wasm/       ewi_wasm.c / build_wasm.ps1 … WASMバインディング+ビルド(Pico/HQの2種)
  web/        index.html / app.js / seq.js / ewi_synth.js(.wasm) / ewi_synth_hq.js(.wasm) … 動作確認アプリ
  pico/       main.c / CMakeLists.txt     … Pico2 I2S+MIDI UART例
```

検証済み: `emcc 6.0.9 / node 24 / Pico(rms=0.06) + PC-HQ(rms=0.07)` で発音確認。

## 音質モード (条件コンパイル `EWI_QUALITY`)

| モード | ビルド | 内容 |
|---|---|---|
| 0 Pico (既定) | Pico FW / `web/ewi_synth.js` | 等倍ラダー近似+高速tanh近似+簡易フォルマント。実機と同一DSP |
| 1 PC-HQ | `-DEWI_QUALITY=1` / `web/ewi_synth_hq.js` | ラダー2xオーバーサンプリング+libm tanh+RBJフォルマント (850Hz/Q4.5, 2300Hz/Q6) |

同一フレーズのrms差は約0.01以内 (別実装のため完全一致はしない)。`Ewi_Quality()` でビルドモードを取得できる。

Webアプリのヘッダ「音質」セレクトで Pico2/PC-HQ を切替可能 (再生中切替可、プリセット・パラメータ・ブレスを引き継いで再初期化)。デモシーケンスやEWI演奏でA/B比較できる。

## 音作り (Axis基準)

* OSC: 2x PolyBLEP Saw/Pulse/Tri/Sine、デチューン、Pulse幅可変
* FILTER: 4段ラダー近似 + tanh飽和 + レゾナンス(0..0.95)、カットオフはオーディオレート平滑
* FORMANT: ラダー段差分の軽量フォルマントブレンド (管の鼻抜け用)
* VCA: ブレス包絡の二乗カーブ (`env^2`)、タンギングのキレ用アタック 4ms / リリース 90ms
* PITCH: レガート優先モノ、グライド、PitchBend、CC1ビブラート(LFO→ピッチ)
* DELAY: モノラループ+分数タップ (0.05〜0.5s、FB 0〜0.7、ループ内ダンピング付き)
* REVERB: Freeverb系モノ (8コム+4オールパス) + Rch 18ms拡散でステレオ化。部屋サイズで残響調整

プリセット (末尾5値は dlyMix/dlyTime/dlyFb/revMix/revSize):

| No | 名前 | 内容 |
|----|------|------|
| 0 | Axis | 2Saw +7cent、base750Hz+breath6200Hz、reso0.38、Dly0.12/0.27s/0.32、Rev0.16/0.55 |
| 1 | Flute | Sine+Tri、reso低め、フォルマント薄め、Dly0.10/0.30s/0.30、Rev0.20/0.50 |
| 2 | Brass | Saw+Pulse(35%)、ドライブ強め、フォルマント濃いめ、Dly0.10/0.24s/0.30、Rev0.14/0.50 |
| 3 | SyncLd | Saw+Pulse(22%) +14cent、reso0.45、Dly0.14/0.29s/0.38、Rev0.18/0.60 |

## MIDIチャート (EWI5000前提、受信ch=1固定)

ステータスは `Ewi_Midi(status, d1, d2)` にそのまま渡せる。

| メッセージ | 例 (hex) | 動作 |
|---|---|---|
| Note On/Off | `90 nn vv` / `80 nn 40` | モノレガート。`vv=0`はNoteOff扱い。velは参照のみ(強弱はブレス) |
| Breath CC2 | `B0 02 vv` | **主包絡**。0..127→0..1。thresh/max+指数カーブ整形→VCA+VCF |
| Breath CC102 | `B0 66 vv` | CC2と等価の代替ブレス (EWI側でCC102を使う場合) |
| Modulation CC1 | `B0 01 vv` | ビブラート深さ。0で無効、127でpresetのmax semitone |
| Volume CC7 | `B0 07 vv` | マスター(初期127) |
| Expression CC11 | `B0 0B vv` | 表情付け(初期127)。VCAに `0.3+0.7*expr` で乗算 |
| Glide CC5 | `B0 05 vv` | グライドタイム `vv/127*0.4s`。CC65なしでも有効。0でプリセット値に戻る |
| Portamento On/Off CC65 | `B0 41 vv` | >=64でON (レガート挙動用。CC5とは独立) |
| PitchBend | `E0 ll hh` (14bit) | `±bend_range` (preset既定±2st)。EWI5000のバイト/サム操作用 |
| Program Change | `C0 pp` | `pp % 4` でプリセット切替 |
| All Sound Off / All Notes Off | `B0 78 00` / `B0 7B 00` | 発音停止+ブレス0 |

EWI5000側の推奨設定:

* Breath Sensor → CC2、Bite (バイト) → PitchBend または CC1
* Bend Range: 本コア±2stに合わせてEWI側も±2
* Velは固定でも可 (本コアはCC2でダイナミクスを付ける)

UIスライダ/キーボード操作時は `CC2/CC1` と等価 (`ewi_cc`) を呼ぶ。
自作ブレスセンサ(ADC直結)の場合は `Ewi_SetBreath01(0..1)` を1kHz以上で更新する。

## ビルド / 動作確認

### 1) WASM + Web (検証用、今回ビルド済み)

```powershell
powershell -ExecutionPolicy Bypass -File wasm/build_wasm.ps1
# -> web/ewi_synth.js + web/ewi_synth.wasm       [Pico]
# -> web/ewi_synth_hq.js + web/ewi_synth_hq.wasm [PC-HQ]
cd web
python -m http.server 8000
# Chrome/Edgeで http://localhost:8000/ を開く
```

操作:

1. `Audio開始` → `MIDI有効化` → セレクトで `EWI5000` を選択
2. EWI5000で吹く (Note + CC2)。PCのみでも `Space`=ブレス110 + `A W S E D...`鍵盤で確認可
3. 波形メータの `level/cutoff` とMIDIモニタでCC2が動くことを確認

### シーケンサ (確認用、著作権配慮)

市販曲のシーケンスデータは同梱していない。Webアプリの「シーケンサ」パネルで以下ができる:

1. `デモフレーズをセット` → `シーケンス再生`: 内蔵のオリジナル表現付き8小節 (Aマイナー基調、bpm100、約19秒) をWASMコアで再生。しゃくり/フォール/ポルタメント/ブレスswellによるフィルタ開閉/区間別フィルタ (前半暗め→後半明るく→終止で閉じる) 入り。再生開始時にPreset 0 (Axis) に切り替わり、終了時にフィルタ等を既定に戻す。Tempoスライダ (0.5x〜1.5x)、loop対応
2. `SMF読込(.mid)`: お手持ちのSMFをブラウザ内でのみ解析して再生 (外部送信なし)。Note / CC1 / CC2 / PitchBend / Program / 先頭Tempoに対応。全chをマージしてモノ再生する。中間テンポチェンジとSMPTE形式は非対応。CC2レーンのないSMFは各ノートの固定ブレスで鳴らす

特定の市販曲 (El Mirage等) の採譜データは付けられないが、ご自身で用意したSMFを読み込ませれば、ブレスの乗り・フィルターの開き・タンギングのキレを本コアで確認できる。

Nodeでのヘッドレス確認 (Pico/HQ両版):

```powershell
& "node.exe" test_wasm.js
# {"mod":"./ewi_synth.js","quality":0,...} + {"mod":"./ewi_synth_hq.js","quality":1,...} + OK
& "node.exe" test_seq.js
# SEQ-OK (シーケンサ配線回帰)
& "node.exe" test_smoke.js
# SMOKE-OK (app.jsの起動〜pump駆動回帰)
```

### 2) Pico2 (RP2350)

* `pico/main.c` はコア呼び出し例。I2S DAC (PCM5102: BCK=GP7, LRCK=GP8, DIN=GP9)、MIDI IN (UART0 RX=GP1, 31250bps) 想定
* `CMakeLists.txt` の `PICO_SDK_PATH` を指定してビルド。`EWI_QUALITY` 未指定=0 (Pico版)。I2SのDMA駆動はボードに合わせて `Ewi_RenderS16Mono()` の前後に追加する
* コアは `sr` と `frames` 以外ハード依存なし。`Ewi_Midi()` にUART受信3バイトを渡すだけ
* 空間系バッファで `EwiSynth` は約170KB。static/global配置すること (スタック禁止)。Pico2の520KB SRAMに収まる。Delay/Reverbの1サンプル当たり演算は20回程度の遅延線操作のみで、M33に十分軽い

### ビルドスクリプトの文字コード注意

* `wasm/build_wasm.ps1` は UTF-8 **BOM付き**で保存すること (現状そうなっている)。PowerShell 5.1はBOMなしUTF-8をcp932解釈し、マルチバイト末尾バイト(例: ト=0x88)の直後の改行が飲み込まれて行がコメント化されることがある。BOMなし保存やPS既定エンコーディングでの書換えは禁止

### 3) PCネイティブ

`core/` の2ファイルをそのままリンクする。例 (MinGW/MSVC/clang共通):

```c
EwiSynth s; Ewi_Init(&s, 48000.0f);
Ewi_Midi(&s, 0x90, 69, 100); Ewi_Midi(&s, 0xB0, 2, 100);
Ewi_Render(&s, bufL, bufR, 512);
```

### 4) VST3プラグイン (PC用、EWI Axis VA)

DAWで使うVST3音源。Pico2と同一のDSPコアを `EWI_QUALITY=1` (PC高品位) でビルド。

| 方式 | メリット | デメリット | 備考 |
|---|---|---|---|
| Steinberg VST3 SDK直結 (採用) | 公式標準・全DAW対応、依存追加なし・軽量、GPLv3/商用選択可 | 自前GUIなし(汎用エディタ)、VST3のみ | 本リポジトリの方式 |
| JUCE | GUIが豪華、VST3/AU/AAX一括、情報が多い | 巨大依存、商用は有料、学習コスト大 | 将来GUI付き化するなら候補 |
| iPlug2 | 軽量・MIT系、VST3/AU/CLAP | コミュニティ小、習得コスト | 中間案 |
| CLAP単体 | 新標準・開放的 | 対応DAWが少ない | 現時点では非推奨 |

前提: CMake 4.x + VS2017 (MSVC 14.14) + git。SDKは `vst/fetch_sdk.ps1` がpinned tag (v3.7.9_build_61) を取得する (`vst/third_party/` はgit管理外)。

```powershell
powershell -ExecutionPolicy Bypass -File vst/fetch_sdk.ps1  # 初回のみ
powershell -ExecutionPolicy Bypass -File vst/build_vst.ps1
# -> vst/build/VST3/Release/ewi-axis-va.vst3
# ewi-axis-va.vst3フォルダごと Common\VST3 (例: %LOCALAPPDATA%\Programs\Common\VST3\) にコピー
```

仕様: モノフォニック・ステレオアウト、16パラメータ (Preset/Breath=CC2/Vibrato=CC1/Volume/Expression/Cutoff/…/Delay/Reverb/Bypass)、MIDI CC受信 (Note/CC1/CC2/CC5/CC7/CC11/CC65/CC102/PitchBend/ProgramChange)+NoteExpressionチューニング対応、IMidiMapping対応 (CC1/2/5/7/11/102)、64bit処理対応、テール1s。GUIはDAW汎用エディタ。

検証: Steinberg validator **47/47通過** (32bit+64bit、複数サンプルレート、可変ブロック、バイパス永続化含む)。`vst/test/bend_test.cpp` (要ビルド) でBEND繰り返し追従をプロセッサ層で検証 (legacy CC129・note-expression両経路)。

## API (抜粋)

```c
void Ewi_Init(&s, sampleRate);
void Ewi_SetPreset(&s, no);            // 0..3
void Ewi_Midi(&s, status, d1, d2);     // Pico/Web共通入口
void Ewi_NoteOn/NoteOff/CC/PitchBend
void Ewi_SetBreath01(&s, 0..1);        // ADC直結用
void Ewi_Render(&s, outL, outR, n);    // float stereo
void Ewi_RenderS16Mono(&s, out, n);    // Pico I2S用
```

パラメータ微調整: `Ewi_SetCutoffBase / SetResonance / SetBreathDepth / SetGlide / SetFormantMix`
空間系: `Ewi_SetDelayMix / SetDelayTime / SetDelayFb / SetRevMix / SetRevSize` (MIDI CC割当はなし、UI/プリセットのみ)

## 既知の制限

* 受信chは1固定 (omni未実装)。必要なら `Ewi_Midi` のch判定を拡張
* フォルマントは軽量近似 (RBJ厳密計算なし)。WASM/Pico共通のCPU節約のため
* Webアプリは先読みスケジューラ方式のため遅延 ~150ms。演奏用低遅延が必要ならAudioWorklet化する (コアはそのまま移植可)
* `pico/main.c` のMIDIパーサは最小形。実機ではリングバッファ+ランニングステータス完全対応を推奨
