# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
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
  New-Item `
    -ItemType Directory `
    -Path $DestinationDirectory `
    -Force | Out-Null
}

$rootName = "clang+llvm-$Version-x86_64-pc-windows-msvc"
$llvmRoot = Join-Path $DestinationDirectory $rootName
if (Test-Path $llvmRoot) {
  throw "LLVM destination already exists: $llvmRoot"
}

$archivePath = Join-Path `
  $env:RUNNER_TEMP `
  "fsim-llvm-$Version-$([Guid]::NewGuid().ToString('N')).tar.xz"
$stagingDirectory = Join-Path `
  $env:RUNNER_TEMP `
  "fsim-llvm-stage-$([Guid]::NewGuid().ToString('N'))"

try {
  Write-Host "Downloading LLVM $Version from $ArchiveUrl"
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
    Get-FileHash `
      -LiteralPath $archivePath `
      -Algorithm SHA256
  ).Hash.ToLowerInvariant()
  if ($actualSha256 -ne $Sha256.ToLowerInvariant()) {
    throw (
      "LLVM archive checksum mismatch: expected {0}, found {1}" `
      -f $Sha256.ToLowerInvariant(), $actualSha256
    )
  }

  New-Item `
    -ItemType Directory `
    -Path $stagingDirectory | Out-Null

  Write-Host "Decompressing verified LLVM archive with 7-Zip"
  & 7z.exe x $archivePath "-o$stagingDirectory" -y

  $tarArchives = @(
    Get-ChildItem `
      -LiteralPath $stagingDirectory `
      -File `
      -Filter "*.tar"
  )
  if ($tarArchives.Count -ne 1) {
    throw (
      "Expected one TAR archive after XZ decompression, found {0}" `
      -f $tarArchives.Count
    )
  }

  Write-Host "Extracting LLVM TAR archive with 7-Zip"
  & 7z.exe x `
    $tarArchives[0].FullName `
    "-o$DestinationDirectory" `
    -y
} finally {
  Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
  Remove-Item `
    -LiteralPath $stagingDirectory `
    -Recurse `
    -Force `
    -ErrorAction SilentlyContinue
}

$llvmConfig = Join-Path $llvmRoot "lib/cmake/llvm/LLVMConfig.cmake"
if (-not (Test-Path -PathType Leaf $llvmConfig)) {
  throw "LLVMConfig.cmake was not present in the development archive"
}

$llvmConfigExecutable = Join-Path $llvmRoot "bin/llvm-config.exe"
$installedVersion = (& $llvmConfigExecutable --version).Trim()
if ($installedVersion -ne $Version) {
  throw "Expected LLVM $Version, found $installedVersion"
}

Add-Content `
  -LiteralPath $env:GITHUB_ENV `
  -Value "LLVM_PATH=$llvmRoot" `
  -Encoding utf8
Add-Content `
  -LiteralPath $env:GITHUB_PATH `
  -Value (Join-Path $llvmRoot "bin") `
  -Encoding utf8

Write-Host "Installed and validated LLVM $installedVersion at $llvmRoot"
