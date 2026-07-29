# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
  [ValidateSet("x86", "x64", "arm64")]
  [string]$Architecture = "x64",

  [ValidateSet("x86", "x64", "arm64")]
  [string]$HostArchitecture = "x64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

if ([string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) {
  throw "GITHUB_ENV is required"
}
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
  throw "RUNNER_TEMP is required"
}

$programFilesX86 = [Environment]::GetEnvironmentVariable(
  "ProgramFiles(x86)"
)
if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
  throw "ProgramFiles(x86) is required"
}
$vswhere = Join-Path `
  $programFilesX86 `
  "Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path -PathType Leaf $vswhere)) {
  throw "vswhere.exe was not found at $vswhere"
}

$installationPaths = & $vswhere `
  -latest `
  -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if ($LASTEXITCODE -ne 0) {
  throw "vswhere.exe failed with exit code $LASTEXITCODE"
}
$installationPath = $installationPaths | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($installationPath)) {
  throw "A Visual Studio installation with the C++ toolchain was not found"
}
$installationPath = $installationPath.Trim()

$developerCommand = Join-Path `
  $installationPath `
  "Common7/Tools/VsDevCmd.bat"
if (-not (Test-Path -PathType Leaf $developerCommand)) {
  throw "VsDevCmd.bat was not found at $developerCommand"
}

$before = [System.Collections.Generic.Dictionary[string, string]]::new(
  [StringComparer]::OrdinalIgnoreCase
)
Get-ChildItem Env: | ForEach-Object {
  $before[$_.Name] = $_.Value
}

$batchPath = Join-Path `
  $env:RUNNER_TEMP `
  "fsim-vsdevcmd-$([Guid]::NewGuid().ToString('N')).cmd"
$batch = @"
@echo off
call "$developerCommand" -no_logo -arch=$Architecture -host_arch=$HostArchitecture >nul
if errorlevel 1 exit /b %errorlevel%
set
"@

try {
  Set-Content `
    -LiteralPath $batchPath `
    -Value $batch `
    -Encoding ascii
  $environmentLines = & $env:ComSpec /d /c $batchPath
  if ($LASTEXITCODE -ne 0) {
    throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
  }
} finally {
  Remove-Item -LiteralPath $batchPath -Force -ErrorAction SilentlyContinue
}

$exported = 0
foreach ($line in $environmentLines) {
  $separator = $line.IndexOf("=")
  if ($separator -le 0) {
    continue
  }

  $name = $line.Substring(0, $separator)
  $value = $line.Substring($separator + 1)
  if (
    $name -eq "CI" `
    -or $name -match "^(ACTIONS_|GITHUB_|RUNNER_)"
  ) {
    continue
  }

  $oldValue = ""
  if (
    $before.TryGetValue($name, [ref]$oldValue) `
    -and $oldValue -eq $value
  ) {
    continue
  }

  Add-Content `
    -LiteralPath $env:GITHUB_ENV `
    -Value "$name=$value" `
    -Encoding utf8
  $exported += 1
}

Write-Host (
  "Configured Visual Studio at $installationPath for host "
  + "$HostArchitecture, target $Architecture; exported $exported "
  + "environment changes"
)
