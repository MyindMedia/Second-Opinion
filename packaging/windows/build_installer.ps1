<#
Builds "Second Opinion <version> Setup.exe" with Inno Setup.

  pwsh packaging/windows/build_installer.ps1 [-ArtefactsDir <dir>] [-OutputDir <dir>] [-Version <x.y.z>]

Defaults: build/win-x64-release/SecondOpinion_artefacts/Release, build/packages, and the version
from CMakeLists.txt (or $env:SO_VERSION).

Authenticode signing is driven by environment variables and skipped when they are unset:

  WIN_SIGN_PFX_BASE64     base64 of a .pfx code-signing certificate
  WIN_SIGN_PFX_PASSWORD   its password
  WIN_SIGN_CERT_SHA1      alternative: thumbprint of a certificate already in the store
                          (hardware token or cloud HSM with a CSP/KSP installed)
  WIN_SIGN_TIMESTAMP_URL  RFC 3161 timestamp server (default http://timestamp.digicert.com)

The plugin DLL, the Standalone exe and the installer are signed. onnxruntime.dll is already
signed by Microsoft and is left alone.
#>
param(
    [string]$ArtefactsDir,
    [string]$OutputDir,
    [string]$Version
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $ArtefactsDir) { $ArtefactsDir = Join-Path $repo 'build\win-x64-release\SecondOpinion_artefacts\Release' }
if (-not $OutputDir)    { $OutputDir    = Join-Path $repo 'build\packages' }
if (-not $Version)      { $Version      = $env:SO_VERSION }
if (-not $Version) {
    $m = Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(SecondOpinion VERSION ([0-9.]+)'
    $Version = $m.Matches[0].Groups[1].Value
}
$ArtefactsDir = (Resolve-Path $ArtefactsDir).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

$name = 'Second Opinion'
$vst3Dll = Join-Path $ArtefactsDir "VST3\$name.vst3\Contents\x86_64-win\$name.vst3"
$exe     = Join-Path $ArtefactsDir "Standalone\$name.exe"
foreach ($f in @($vst3Dll, $exe)) {
    if (-not (Test-Path $f)) { throw "Missing $f (build the win-x64-release preset first)" }
}

# ---- Signing setup ---------------------------------------------------------------------------
function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = Get-ChildItem $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw 'signtool.exe not found (install the Windows 10/11 SDK)' }
    return $found.FullName
}

$signArgs = $null
$pfxPath = $null
$timestamp = if ($env:WIN_SIGN_TIMESTAMP_URL) { $env:WIN_SIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
if ($env:WIN_SIGN_PFX_BASE64) {
    $pfxPath = Join-Path ([IO.Path]::GetTempPath()) ("so-sign-" + [Guid]::NewGuid() + '.pfx')
    [IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($env:WIN_SIGN_PFX_BASE64))
    $signArgs = @('sign', '/fd', 'SHA256', '/td', 'SHA256', '/tr', $timestamp, '/f', $pfxPath)
    if ($env:WIN_SIGN_PFX_PASSWORD) { $signArgs += @('/p', $env:WIN_SIGN_PFX_PASSWORD) }
} elseif ($env:WIN_SIGN_CERT_SHA1) {
    $signArgs = @('sign', '/fd', 'SHA256', '/td', 'SHA256', '/tr', $timestamp, '/sha1', $env:WIN_SIGN_CERT_SHA1)
}

function Invoke-Sign([string]$file) {
    if (-not $signArgs) { return }
    $tool = Find-SignTool
    & $tool @signArgs /d $name $file
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for $file" }
    & $tool verify /pa $file
    if ($LASTEXITCODE -ne 0) { throw "signature verification failed for $file" }
}

try {
    # Sign copies, not the build tree.
    $stage = Join-Path ([IO.Path]::GetTempPath()) ("so-installer-" + [Guid]::NewGuid())
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -Recurse (Join-Path $ArtefactsDir 'VST3') (Join-Path $stage 'VST3')
    Copy-Item -Recurse (Join-Path $ArtefactsDir 'Standalone') (Join-Path $stage 'Standalone')

    if ($signArgs) {
        Write-Host 'Signing binaries'
        Invoke-Sign (Join-Path $stage "VST3\$name.vst3\Contents\x86_64-win\$name.vst3")
        Invoke-Sign (Join-Path $stage "Standalone\$name.exe")
    } else {
        Write-Host 'No signing variables set: building an unsigned installer.'
    }

    $iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
    if (-not $iscc) { $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if (-not (Test-Path $iscc)) { throw 'Inno Setup 6 (ISCC.exe) not found' }

    & $iscc "/DAppVersion=$Version" "/DArtefactsDir=$stage" "/DOutputDir=$OutputDir" (Join-Path $PSScriptRoot 'SecondOpinion.iss')
    if ($LASTEXITCODE -ne 0) { throw 'ISCC failed' }

    $setup = Join-Path $OutputDir "$name $Version Setup.exe"
    Invoke-Sign $setup
    Write-Host "Installer: $setup"
} finally {
    if ($pfxPath -and (Test-Path $pfxPath)) { Remove-Item -Force $pfxPath }
    if ($stage -and (Test-Path $stage)) { Remove-Item -Recurse -Force $stage }
}
