param(
    [string]$Name = 'boot3',
    [int]$Seconds = 12,
    [switch]$DefaultBoot
)
$ErrorActionPreference = 'Stop'
$pes = Split-Path -Parent $PSScriptRoot
$local = Split-Path -Parent $pes
$exe = (Resolve-Path (Join-Path $local 'ps2-build/ps2xRuntime/Release/PES2014-Recompiled.exe')).Path
$elf = (Resolve-Path (Join-Path $pes 'disc/SLES_556.76')).Path
$out = Join-Path $PSScriptRoot "$Name.stdout.log"
$err = Join-Path $PSScriptRoot "$Name.stderr.log"
$launch = @{
    FilePath = $exe
    WorkingDirectory = $PSScriptRoot
    WindowStyle = 'Hidden'
    PassThru = $true
    RedirectStandardOutput = $out
    RedirectStandardError = $err
}
if (!$DefaultBoot) { $launch.ArgumentList = '"' + $elf + '"' }
$game = Start-Process @launch
if ($game.WaitForExit($Seconds * 1000)) {
    "Exit code: $($game.ExitCode)"
} else {
    $game.Refresh()
    $status = "Still active after $Seconds seconds. Window title: $($game.MainWindowTitle)"
    Stop-Process -Id $game.Id
    $game.WaitForExit(5000) | Out-Null
    $status
}
Get-Content $out -Tail 16
Get-Content $err -Tail 34
