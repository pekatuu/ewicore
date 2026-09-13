# Re-fetch the pinned VST3 SDK (no submodules needed beyond base/cmake/pluginterfaces/public.sdk)
$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $PSScriptRoot
$SDK = Join-Path $ROOT "third_party\vst3sdk"
if (!(Test-Path $SDK)) {
  git clone --depth 1 --branch v3.7.9_build_61 https://github.com/steinbergmedia/vst3sdk.git $SDK
}
git -C $SDK submodule update --init --depth 1 base cmake pluginterfaces public.sdk
Write-Host "SDK ready at $SDK"
