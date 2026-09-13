# EWI5000用 VAシンセコア: WASMビルド (Pico版 + PC高品位版の2種)
$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $PSScriptRoot
$EMCC = "path\to\emcc.exe"
if (!(Test-Path $EMCC)) { throw "emcc not found: $EMCC" }

$FUNCS = '["_ewi_init","_ewi_reset","_ewi_noteOn","_ewi_noteOff","_ewi_cc","_ewi_pitchbend","_ewi_program","_ewi_midi","_ewi_setBreath","_ewi_setCutoff","_ewi_setReso","_ewi_setBreathDepth","_ewi_setGlide","_ewi_setFormant","_ewi_setDlyMix","_ewi_setDlyTime","_ewi_setDlyFb","_ewi_setRevMix","_ewi_setRevSize","_ewi_getLevel","_ewi_getCutoff","_ewi_quality","_ewi_render","_ewi_ptrL","_ewi_ptrR","_malloc","_free"]'
$RUNTIME = '["HEAPF32","HEAPU8"]'
$SRC = @("$ROOT\wasm\ewi_wasm.c", "$ROOT\core\ewi_synth.c")

# 1) Pico2版 (EWI_QUALITY=0): 実機と同一の軽量DSP
& $EMCC $SRC -O3 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=EwiModule -s ALLOW_MEMORY_GROWTH=1 -s MALLOC=emmalloc -s EXPORTED_FUNCTIONS=$FUNCS -s EXPORTED_RUNTIME_METHODS=$RUNTIME -o "$ROOT\web\ewi_synth.js"
if ($LASTEXITCODE -ne 0) { throw "Pico build failed: $LASTEXITCODE" }

# 2) PC高品位版 (EWI_QUALITY=1): 2xOSラダー+tanhf+RBJフォルマント
& $EMCC $SRC -O3 -DEWI_QUALITY=1 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=EwiModuleHQ -s ALLOW_MEMORY_GROWTH=1 -s MALLOC=emmalloc -s EXPORTED_FUNCTIONS=$FUNCS -s EXPORTED_RUNTIME_METHODS=$RUNTIME -o "$ROOT\web\ewi_synth_hq.js"
Write-Host ("HQ exit=" + $LASTEXITCODE)
if ($LASTEXITCODE -ne 0) { throw "HQ build failed: $LASTEXITCODE" }

Write-Host "built -> web/ewi_synth.js(.wasm) [Pico] + web/ewi_synth_hq.js(.wasm) [PC-HQ]"

# 無音no-op防止: 出力4点の存在を検査
foreach ($f in @("$ROOT\web\ewi_synth.js", "$ROOT\web\ewi_synth.wasm", "$ROOT\web\ewi_synth_hq.js", "$ROOT\web\ewi_synth_hq.wasm")) {
  if (!(Test-Path $f)) { throw "missing output: $f" }
  Write-Host ("ok " + $f)
}
