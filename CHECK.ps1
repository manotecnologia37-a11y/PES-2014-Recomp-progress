#requires -Version 5.1
<#
.SYNOPSIS
    Environment check for the PES 2014 (PS2) recompilation share pack.

.DESCRIPTION
    Verifies the toolchain, the extracted game layout and the repository state before you
    start the (long) build. Read-only: it changes nothing.

.PARAMETER WorkDir
    Folder that contains disc/ , output/ and SLES_556.76 .
    Default: the folder next to this script named "pes2014", or this script's folder.

.PARAMETER RepoDir
    PS2Recomp checkout.
    Default: a "PS2Recomp" folder next to WorkDir, or one level above this script.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\CHECK.ps1
    powershell -ExecutionPolicy Bypass -File .\CHECK.ps1 -WorkDir D:\pes2014 -RepoDir D:\PS2Recomp
#>
param(
    [string]$WorkDir = '',
    [string]$RepoDir = ''
)

$ErrorActionPreference = 'Continue'

# Expected upstream commit for the shipped patch.
$ExpectedCommit = 'e563698'

$script:Fail = 0
$script:Warn = 0
$results = New-Object System.Collections.Generic.List[object]

function Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail = '',
        [ValidateSet('fail', 'warn')][string]$Severity = 'fail'
    )
    $state = 'OK'
    if (-not $Ok) {
        if ($Severity -eq 'warn') { $state = 'WARN'; $script:Warn++ }
        else { $state = 'FAIL'; $script:Fail++ }
    }
    $results.Add([pscustomobject]@{ State = $state; Name = $Name; Detail = $Detail })
    Write-Host ('[{0,4}] {1,-32} {2}' -f $state, $Name, $Detail)
}

function FirstLine {
    param($Text)
    if (-not $Text) { return '' }
    $first = @($Text)[0]
    if ($null -eq $first) { return '' }
    return ([string]$first).Trim()
}

function Info {
    param([string]$Name, [string]$Detail = '')
    Write-Host ('[{0,4}] {1,-32} {2}' -f 'INFO', $Name, $Detail)
    $results.Add([pscustomobject]@{ State = 'INFO'; Name = $Name; Detail = $Detail })
}

Write-Host ''
Write-Host '=== PES 2014 recompilation - environment check ==='
Write-Host ''

# ---------------------------------------------------------------- OS
Check 'Windows 64-bit' ([Environment]::Is64BitOperatingSystem) `
    ([Environment]::OSVersion.VersionString)

# ---------------------------------------------------------------- CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeVersion = ''
$cmakeOk = $false
if ($cmake) {
    $cmakeVersion = FirstLine (& cmake --version 2>$null)
    if ($cmakeVersion -match '(\d+)\.(\d+)') {
        $major = [int]$Matches[1]; $minor = [int]$Matches[2]
        $cmakeOk = ($major -gt 3) -or ($major -eq 3 -and $minor -ge 25)
    }
}
Check 'CMake >= 3.25' $cmakeOk ($(if ($cmake) { $cmakeVersion } else { 'not found in PATH' }))

# ---------------------------------------------------------------- Python
$python = Get-Command python -ErrorAction SilentlyContinue
$pyVersion = ''
$pyOk = $false
if ($python) {
    $pyVersion = FirstLine (& python --version 2>&1)
    if ($pyVersion -match '(\d+)\.(\d+)') {
        $pyOk = ([int]$Matches[1] -ge 3)
    }
}
Check 'Python 3' $pyOk ($(if ($python) { $pyVersion } else { 'not found in PATH (needed by the overlay scripts)' }))

# ---------------------------------------------------------------- Git
$git = Get-Command git -ErrorAction SilentlyContinue
$gitVersion = ''
if ($git) { $gitVersion = FirstLine (& git --version 2>$null) }
Check 'Git' ([bool]$git) ($(if ($git) { $gitVersion } else { 'not found in PATH' })) 'warn'

# ---------------------------------------------------------------- Visual Studio / MSVC
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsDetail = 'not found (install Visual Studio 2022 with "Desktop development with C++")'
$vsOk = $false
if (Test-Path $vswhere) {
    $vsPath = FirstLine (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
    if ($vsPath) {
        $vsOk = $true
        $vsDetail = $vsPath
    } else {
        $vsDetail = 'Visual Studio found, but the C++ x64 workload is missing'
    }
}
Check 'Visual Studio (MSVC x64)' $vsOk $vsDetail

# ---------------------------------------------------------------- Ghidra (optional)
$ghidraCandidates = @()
if ($env:GHIDRA_INSTALL_DIR) { $ghidraCandidates += $env:GHIDRA_INSTALL_DIR }
if ($env:GHIDRA_HOME) { $ghidraCandidates += $env:GHIDRA_HOME }
$ghidraCandidates += (Get-ChildItem -Path 'C:\' -Filter 'ghidra*' -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$ghidraCandidates += (Get-ChildItem -Path (Join-Path $env:USERPROFILE 'tools') -Filter 'ghidra*' -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$ghidra = $ghidraCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
Check 'Ghidra (optional)' ([bool]$ghidra) `
    ($(if ($ghidra) { $ghidra } else { 'not found - only needed to (re)generate functions.csv' })) 'warn'

# ---------------------------------------------------------------- paths
$shareDir = $PSScriptRoot
if (-not $WorkDir) {
    $candidate = Join-Path (Split-Path $shareDir -Parent) 'pes2014'
    if (Test-Path $candidate) { $WorkDir = $candidate } else { $WorkDir = Split-Path $shareDir -Parent }
}
if (-not $RepoDir) {
    $candidate = Join-Path (Split-Path $shareDir -Parent) 'PS2Recomp'
    if (-not (Test-Path $candidate)) { $candidate = Join-Path (Split-Path (Split-Path $shareDir -Parent) -Parent) 'PS2Recomp' }
    if (Test-Path $candidate) { $RepoDir = $candidate }
}
Check 'Work folder' ([bool]$WorkDir -and (Test-Path $WorkDir)) $WorkDir
Check 'PS2Recomp checkout' ([bool]$RepoDir -and (Test-Path (Join-Path $RepoDir 'ps2xRuntime'))) `
    ($(if ($RepoDir) { $RepoDir } else { 'not found - pass -RepoDir' }))

# ---------------------------------------------------------------- game data
$disc = if ($WorkDir) { Join-Path $WorkDir 'disc' } else { '' }
Check 'disc/SLES_556.76' ([bool]$disc -and (Test-Path (Join-Path $disc 'SLES_556.76'))) $disc
Check 'disc/SYSTEM.CNF' ([bool]$disc -and (Test-Path (Join-Path $disc 'SYSTEM.CNF'))) 'required by the runtime'
Check 'disc/OVER.AFS' ([bool]$disc -and (Test-Path (Join-Path $disc 'OVER.AFS'))) 'needed to extract the overlays'
Check 'disc/IOP' ([bool]$disc -and (Test-Path (Join-Path $disc 'IOP'))) 'IOP modules'
$elf = if ($WorkDir) { Join-Path $WorkDir 'SLES_556.76' } else { '' }
Check 'SLES_556.76 (recompiler input)' ([bool]$elf -and (Test-Path $elf)) `
    'copy it out of disc/ to the work folder root'

# ---------------------------------------------------------------- repo state and patch
$patch = Join-Path $shareDir 'patches\pes2014-runtime.patch'
$patchItem = Get-Item $patch -ErrorAction SilentlyContinue
Check 'Patch file' ([bool]$patchItem) `
    ($(if ($patchItem) { '{0:N1} KB' -f ($patchItem.Length / 1KB) } else { 'missing: patches\pes2014-runtime.patch' }))

if ($git -and $RepoDir -and (Test-Path (Join-Path $RepoDir '.git'))) {
    $head = FirstLine (& git -C $RepoDir rev-parse --short HEAD 2>$null)
    $dirty = (& git -C $RepoDir status --porcelain 2>$null | Measure-Object).Count
    if ($head -eq $ExpectedCommit) {
        Check 'Repo at expected commit' $true "$head (+$dirty local changes)"
    } else {
        Check 'Repo at expected commit' $false "HEAD=$head, patch targets $ExpectedCommit - use 'git apply -3' or checkout $ExpectedCommit" 'warn'
    }
    if ($dirty -gt 0) {
        Info 'Repo already patched?' "$dirty modified/untracked file(s) - expected only AFTER you apply the patch"
    }
}

# ---------------------------------------------------------------- disk space
$drive = if ($WorkDir -and (Test-Path $WorkDir)) { (Get-Item $WorkDir).PSDrive } else { $null }
if ($drive) {
    $freeGb = [math]::Round($drive.Free / 1GB, 1)
    Check 'Free disk space' ($drive.Free -gt 8GB) ("$freeGb GB free (build needs ~4-6 GB: 63k .cpp + 700 MB exe)")
}

# ---------------------------------------------------------------- summary
Write-Host ''
if ($script:Fail -eq 0) {
    if ($script:Warn -eq 0) {
        Write-Host 'Result: READY.' -ForegroundColor Green
    } else {
        Write-Host ("Result: READY with {0} warning(s)." -f $script:Warn) -ForegroundColor Yellow
    }
    Write-Host ''
    Write-Host 'Next steps (see SETUP.md):'
    Write-Host '  4. edit <PES2014_DIR> in config\*.toml'
    Write-Host '  5. generate functions.csv (ps2_analyzer + Ghidra)'
    Write-Host '  6. export + recompile the overlays'
    Write-Host '  7. stage the overlays'
    Write-Host '  8. build ps2EntryRunner'
    Write-Host '  9. run the game'
    Write-Host ''
    exit 0
}

Write-Host ("Result: {0} blocking problem(s), {1} warning(s)." -f $script:Fail, $script:Warn) -ForegroundColor Red
Write-Host ''
Write-Host 'Fix the FAIL lines above, then run this check again.'
Write-Host 'See SETUP.md section 0 for the prerequisites.'
Write-Host ''
exit 1
