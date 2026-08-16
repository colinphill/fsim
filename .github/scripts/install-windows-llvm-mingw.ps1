# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9]{8}$")]
  [string]$Release,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9]+\.[0-9]+\.[0-9]+$")]
  [string]$Version,

  [Parameter(Mandatory)]
  [ValidatePattern("^https://")]
  [string]$ArchiveUrl,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9a-fA-F]{64}$")]
  [string]$Sha256,

  [string]$DestinationDirectory = $env:RUNNER_TEMP
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

foreach ($required in @("GITHUB_ENV", "GITHUB_PATH", "RUNNER_TEMP")) {
  $value = [Environment]::GetEnvironmentVariable($required)
  if ([string]::IsNullOrWhiteSpace($value)) {
    throw "$required is required"
  }
}

if (-not (Test-Path -PathType Container $DestinationDirectory)) {
  New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
}

$rootName = "llvm-mingw-$Release-ucrt-x86_64"
$toolchainRoot = Join-Path $DestinationDirectory $rootName
if (Test-Path $toolchainRoot) {
  throw "LLVM-MinGW destination already exists: $toolchainRoot"
}

$archivePath = Join-Path `
  $env:RUNNER_TEMP `
  "fsim-llvm-mingw-$Release-$([Guid]::NewGuid().ToString('N')).zip"

try {
  Write-Host "Downloading LLVM-MinGW $Release from $ArchiveUrl"
  & curl.exe `
    --fail `
    --location `
    --proto "=https" `
    --retry 3 `
    --retry-delay 2 `
    --silent `
    --show-error `
    --output $archivePath `
    $ArchiveUrl

  $actualSha256 = (
    Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
  ).Hash.ToLowerInvariant()
  if ($actualSha256 -ne $Sha256.ToLowerInvariant()) {
    throw (
      "LLVM-MinGW archive checksum mismatch: expected {0}, found {1}" `
      -f $Sha256.ToLowerInvariant(), $actualSha256
    )
  }

  Write-Host "Extracting verified LLVM-MinGW archive"
  & 7z.exe x $archivePath "-o$DestinationDirectory" -y
} finally {
  Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
}

$requiredFiles = @(
  "bin/clang.exe",
  "bin/clang++.exe",
  "bin/llvm-ar.exe",
  "bin/llvm-ranlib.exe",
  "bin/llvm-windres.exe",
  "busybox/bin/bash.exe",
  "busybox/bin/make.exe"
)
foreach ($relativePath in $requiredFiles) {
  $candidate = Join-Path $toolchainRoot $relativePath
  if (-not (Test-Path -PathType Leaf $candidate)) {
    throw "LLVM-MinGW archive is missing $relativePath"
  }
}

$clang = Join-Path $toolchainRoot "bin/clang.exe"
$installedVersion = (& $clang --version | Select-Object -First 1)
if ($installedVersion -notmatch "clang version $([regex]::Escape($Version))(?:\s|$)") {
  throw "Expected Clang $Version, found: $installedVersion"
}
$target = (& $clang -dumpmachine).Trim()
if ($target -ne "x86_64-w64-windows-gnu") {
  throw "Expected x86_64-w64-windows-gnu, found $target"
}

Add-Content `
  -LiteralPath $env:GITHUB_ENV `
  -Value "LLVM_MINGW_ROOT=$($toolchainRoot.Replace('\', '/'))" `
  -Encoding utf8
Add-Content `
  -LiteralPath $env:GITHUB_PATH `
  -Value (Join-Path $toolchainRoot "bin") `
  -Encoding utf8
Add-Content `
  -LiteralPath $env:GITHUB_PATH `
  -Value (Join-Path $toolchainRoot "busybox/bin") `
  -Encoding utf8

Write-Host "Installed and validated LLVM-MinGW $Release ($Version) at $toolchainRoot"
