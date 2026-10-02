# SPDX-License-Identifier: Apache-2.0

# Install the pinned fsim toolchain (an x86-64 UCRT build of LLVM-MinGW):
# the build toolchain with its LLVM development overlay extracted over it, and
# the redistributable archive that fsim's binary archives bundle. Every
# archive is checked against its SHA-256 before use.
[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9]{8}$")]
  [string]$Release,

  [Parameter(Mandatory)]
  [ValidatePattern("^fsim-toolchain-[0-9]+\.[0-9]+\.[0-9]+-[0-9]+$")]
  [string]$Toolchain,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9]+\.[0-9]+\.[0-9]+$")]
  [string]$Version,

  [Parameter(Mandatory)]
  [ValidatePattern("^https://")]
  [string]$ArchiveUrl,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9a-fA-F]{64}$")]
  [string]$Sha256,

  [Parameter(Mandatory)]
  [ValidatePattern("^https://")]
  [string]$DevArchiveUrl,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9a-fA-F]{64}$")]
  [string]$DevSha256,

  [Parameter(Mandatory)]
  [ValidatePattern("^https://")]
  [string]$RedistArchiveUrl,

  [Parameter(Mandatory)]
  [ValidatePattern("^[0-9a-fA-F]{64}$")]
  [string]$RedistSha256,

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

$rootName = "$Toolchain-ucrt-x86_64"
$toolchainRoot = Join-Path $DestinationDirectory $rootName
if (Test-Path $toolchainRoot) {
  throw "toolchain destination already exists: $toolchainRoot"
}

function Get-VerifiedArchive([string]$Url, [string]$Expected, [string]$ArchivePath) {
  Write-Host "Downloading $Url"
  & curl.exe `
    --fail `
    --location `
    --proto "=https" `
    --retry 3 `
    --retry-delay 2 `
    --silent `
    --show-error `
    --output $ArchivePath `
    $Url
  $actualSha256 = (
    Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256
  ).Hash.ToLowerInvariant()
  if ($actualSha256 -ne $Expected.ToLowerInvariant()) {
    throw (
      "toolchain archive checksum mismatch for {0}: expected {1}, found {2}" `
      -f $Url, $Expected.ToLowerInvariant(), $actualSha256
    )
  }
}

function Read-Manifest([string]$Path) {
  if (-not (Test-Path -PathType Leaf $Path)) {
    throw "toolchain manifest is missing: $Path"
  }
  $records = @{}
  foreach ($line in Get-Content -LiteralPath $Path) {
    if ($line -match "^([a-z_0-9]+)=(.*)$") {
      $records[$Matches[1]] = $Matches[2]
    }
  }
  return $records
}

$buildArchive = Join-Path $env:RUNNER_TEMP "$rootName-$([Guid]::NewGuid().ToString('N')).zip"
$devArchive = Join-Path $env:RUNNER_TEMP "$rootName-llvm-dev-$([Guid]::NewGuid().ToString('N')).zip"
$redistArchive = Join-Path $DestinationDirectory "$rootName-redist.zip"
try {
  Get-VerifiedArchive $ArchiveUrl $Sha256 $buildArchive
  Get-VerifiedArchive $DevArchiveUrl $DevSha256 $devArchive
  Write-Host "Extracting verified toolchain and LLVM development overlay"
  & 7z.exe x $buildArchive "-o$DestinationDirectory" -y
  & 7z.exe x $devArchive "-o$DestinationDirectory" -y
} finally {
  Remove-Item -LiteralPath $buildArchive -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $devArchive -Force -ErrorAction SilentlyContinue
}
Get-VerifiedArchive $RedistArchiveUrl $RedistSha256 $redistArchive

$requiredFiles = @(
  "FSIM-TOOLCHAIN.txt",
  "bin/clang.exe",
  "bin/clang++.exe",
  "bin/lldb.exe",
  "bin/llvm-ar.exe",
  "bin/llvm-ranlib.exe",
  "bin/llvm-windres.exe",
  "busybox/bin/bash.exe",
  "busybox/bin/make.exe",
  "lib/cmake/llvm/LLVMConfig.cmake",
  "lib/libLLVM-22.dll.a",
  "share/fsim-toolchain/llvm-dev.txt"
)
foreach ($relativePath in $requiredFiles) {
  $candidate = Join-Path $toolchainRoot $relativePath
  if (-not (Test-Path -PathType Leaf $candidate)) {
    throw "toolchain archives are missing $relativePath"
  }
}

$build = Read-Manifest (Join-Path $toolchainRoot "FSIM-TOOLCHAIN.txt")
$dev = Read-Manifest (Join-Path $toolchainRoot "share/fsim-toolchain/llvm-dev.txt")
if ($build["name"] -ne $rootName -or $build["variant"] -ne "build" `
    -or $build["upstream"] -ne "llvm-mingw $Release" `
    -or $build["llvm"] -ne "llvmorg-$Version") {
  throw "unexpected build toolchain identity: $($build | Out-String)"
}
if ($dev["variant"] -ne "llvm-dev" -or $dev["fork_commit"] -ne $build["fork_commit"]) {
  throw "the LLVM development overlay comes from a different toolchain build"
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
  -LiteralPath $env:GITHUB_ENV `
  -Value "FSIM_WINDOWS_TOOLCHAIN_REDIST=$($redistArchive.Replace('\', '/'))" `
  -Encoding utf8
Add-Content `
  -LiteralPath $env:GITHUB_PATH `
  -Value (Join-Path $toolchainRoot "bin") `
  -Encoding utf8
Add-Content `
  -LiteralPath $env:GITHUB_PATH `
  -Value (Join-Path $toolchainRoot "busybox/bin") `
  -Encoding utf8

Write-Host "Installed and validated $Toolchain (LLVM-MinGW $Release, LLVM $Version) at $toolchainRoot"
