# Builds dist\TheCalm-<version>-Windows.exe: x64 VST3 and CLAP (they also run on Windows on ARM in
# x64 and ARM64EC hosts).
#   powershell -ExecutionPolicy Bypass -File tools\make_installer_win.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"

$version = (Select-String -Path CMakeLists.txt -Pattern '^project\(TheCalm VERSION ([0-9.]+)').Matches[0].Groups[1].Value

foreach ($arch in @("x64")) {
    $dir = "build-" + $arch.ToLower()
    cmake -B $dir -G "Visual Studio 17 2022" -A $arch | Out-Null
    cmake --build $dir --config Release --target TheCalm_VST3 TheCalm_CLAP -- /m /v:minimal | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "build failed for $arch" }
}

$stage = "build-win\stage"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\VST3", "$stage\CLAP" | Out-Null
Copy-Item -Recurse "build-x64\TheCalm_artefacts\Release\VST3\The Calm.vst3" "$stage\VST3\The Calm.vst3"
Copy-Item "build-x64\TheCalm_artefacts\Release\CLAP\The Calm.clap" "$stage\CLAP\"

New-Item -ItemType Directory -Force dist | Out-Null
& "C:\Program Files\Inno Setup 7\ISCC.exe" /Qp "/DAppVersion=$version" "/DStage=$root\$stage" installer\TheCalm.iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
Get-Item "dist\TheCalm-$version-Windows.exe"
