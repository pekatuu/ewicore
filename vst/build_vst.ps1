# Build EWI Axis VA VST3 (Release x64, VS2017)
$ErrorActionPreference = "Stop"
$VSTDIR = Split-Path -Parent $MyInvocation.MyCommand.Path
if (!(Test-Path (Join-Path $VSTDIR "third_party\vst3sdk\public.sdk"))) {
  throw "VST3 SDK not found. Run vst/fetch_sdk.ps1 first."
}
$SRC = $VSTDIR
$BLD = Join-Path $VSTDIR "build"
cmake -S $SRC -B $BLD -G "Visual Studio 15 2017" -A x64 -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build $BLD --config Release --target ewi-axis-va
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$VST3 = Join-Path $BLD "VST3\Release\ewi-axis-va.vst3"
if (!(Test-Path $VST3)) { throw "missing output: $VST3" }
Write-Host "VST3 ready at $VST3"
Write-Host "Copy the ewi-axis-va.vst3 folder to your VST3 folder, e.g.: $env:LOCALAPPDATA\Programs\Common\VST3\"
