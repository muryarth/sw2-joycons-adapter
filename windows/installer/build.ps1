# Builds the Release app and packages it as an MSI.
#
#   powershell -ExecutionPolicy Bypass -File windows\installer\build.ps1
#
# Needs: Visual Studio 2022 (C++ workload, CMake), .NET SDK and WiX 5:
#   dotnet tool install --global wix --version 5.0.2
#   wix extension add -g WixToolset.UI.wixext/5.0.2
# Output: out\installer\SW2Adapter-<version>-x64.msi
param([switch]$SkipAppBuild)
$ErrorActionPreference = 'Stop'

$repo  = (Resolve-Path "$PSScriptRoot\..\..").Path
$out   = Join-Path $repo 'out'
$build = Join-Path $out 'build'
$dist  = Join-Path $out 'installer'
New-Item -ItemType Directory -Force $dist | Out-Null

# Single source of truth for the version: windows/src/app/Version.h
$versionLine = Select-String -Path "$repo\windows\src\app\Version.h" -Pattern 'APP_VERSION\s*=\s*"([\d.]+)"'
if (-not $versionLine) { throw 'APP_VERSION not found in Version.h' }
$version = $versionLine.Matches[0].Groups[1].Value
Write-Host "SW2 Adapter $version"

if (-not $SkipAppBuild) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $vs = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
    & $cmake -S $repo -B $build -G 'Visual Studio 17 2022' -A x64 | Out-Null
    if ($LASTEXITCODE) { throw 'cmake configure failed' }
    & $cmake --build $build --config Release -- '/m:1' '/nologo' '/verbosity:minimal'
    if ($LASTEXITCODE) { throw 'app build failed' }
}

# The license page wants RTF; generate it from LICENSE so the two never drift.
$licenseRtf = Join-Path $dist 'License.rtf'
$text = (Get-Content "$repo\LICENSE" -Raw) -replace '\\', '\\\\' -replace '\{', '\{' -replace '\}', '\}'
$text = $text -replace "`r?`n", "\par`r`n"
Set-Content -Path $licenseRtf -Encoding ascii -Value "{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\fs18`r`n$text}"

& "$PSScriptRoot\make-art.ps1" -OutDir $dist -IconPath "$repo\assets\app.ico"

$wix = (Get-Command wix -ErrorAction SilentlyContinue).Source
if (-not $wix) { $wix = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe' }
$msi = Join-Path $dist "SW2Adapter-$version-x64.msi"
& $wix build "$PSScriptRoot\Package.wxs" -arch x64 -ext WixToolset.UI.wixext `
    -d "Version=$version" `
    -d "BinDir=$build\windows\Release" `
    -d "AssetsDir=$repo\assets" `
    -d "RepoDir=$repo" `
    -d "LicenseRtf=$licenseRtf" `
    -d "ArtDir=$dist" `
    -o $msi
if ($LASTEXITCODE) { throw 'wix build failed' }
Remove-Item (Join-Path $dist '*.wixpdb') -ErrorAction SilentlyContinue
Write-Host "MSI: $msi"
