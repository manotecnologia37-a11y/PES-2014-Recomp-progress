param(
 [Parameter(Mandatory=$true)][string]$Name,
 [int]$Seconds = 12,
 [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$local = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$log = Join-Path $PSScriptRoot "$Name.build.log"
cmake --build (Join-Path $local 'ps2-build') --config Release --target ps2EntryRunner --parallel $Jobs *> $log
$code = $LASTEXITCODE
if ($code -ne 0) { Get-Content $log -Tail 20; exit $code }
$env:PES2014_BOOT_DIAGNOSTICS = '1'
& (Join-Path $PSScriptRoot 'test-boot.ps1') -Name $Name -Seconds $Seconds | Select-Object -First 1
$matches = rg -n 'PES boot\] thread 1 |missing-target|file open|fatal|trace:unhandled|CDVD search|PES call' (Join-Path $PSScriptRoot "$Name.stderr.log")
$matches | Select-Object -Last 40 | ForEach-Object { $_.Substring(0,[Math]::Min(520,$_.Length)) }
