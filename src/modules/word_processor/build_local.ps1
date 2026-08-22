# Builds the word-processor module locally with the installed Qt 6.8.3 MSVC kit.
param(
    [string]$SourceDir = $PSScriptRoot,
    [string]$QtPath = 'C:\Qt\6.8.3\msvc2022_64',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ toolset not found.' }

$devcmd = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
$build = Join-Path $SourceDir 'build-wp'

if ($Clean -and (Test-Path $build)) {
    Remove-Item -Recurse -Force $build
}

& pwsh -NoProfile -Command ". '$devcmd' -Arch amd64 -HostArch amd64; cmake -S '$SourceDir' -B '$build' -G Ninja -DCMAKE_PREFIX_PATH='$QtPath' -DCMAKE_BUILD_TYPE=Release; if (`$LASTEXITCODE -ne 0) { exit 1 }; cmake --build '$build' --config Release; exit `$LASTEXITCODE"
exit $LASTEXITCODE
