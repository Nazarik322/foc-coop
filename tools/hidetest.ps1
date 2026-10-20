# Manual probe: hide the guest's pawn on the host (Actor.bHidden +0x84 bit 4, bForceNetUpdate +0x8c bit 0x80000) and
# take shots of both before/after.
param([string]$Out = "E:\FoC-coop\sessions\hide1", [string]$Map = "L06_Megatron_BASE_m", [int]$Settle = 100, [switch]$Fresh)
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
if ($Fresh) { & "$PSScriptRoot\matrix.ps1" -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-Object -Last 2 }
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 240) {
  $a = Read-FocState A
  if ($a -and ($a.players | Where-Object { $_.local -eq 1 }).cine -eq 0 -and $a.players.Count -ge 2 -and ((Get-Date) - $t0).TotalSeconds -gt $Settle) { break }
  Start-Sleep 2
}
Send-Cmd A "shot $Out\pre_A.bmp"; Start-Sleep 1; Send-Cmd B "shot $Out\pre_B.bmp"; Start-Sleep 2
Send-Cmd A "poke rpawn 84 |4"; Start-Sleep 1; Send-Cmd A "poke rpawn 8c |0x80000"; Start-Sleep 3
Send-Cmd A "shot $Out\hid_A.bmp"; Start-Sleep 1; Send-Cmd B "shot $Out\hid_B.bmp"; Start-Sleep 2
Send-Cmd A "poke rpawn 84 ~4"; Start-Sleep 1; Send-Cmd A "poke rpawn 8c |0x80000"; Start-Sleep 3
Send-Cmd A "shot $Out\back_A.bmp"; Start-Sleep 1; Send-Cmd B "shot $Out\back_B.bmp"; Start-Sleep 2
Add-Type -AssemblyName System.Drawing
Get-ChildItem $Out -Filter *.bmp | ForEach-Object { $i = [System.Drawing.Image]::FromFile($_.FullName); $i.Save(($_.FullName -replace '\.bmp$', '.jpg'), [System.Drawing.Imaging.ImageFormat]::Jpeg); $i.Dispose() }
Select-String -Path "E:\FoC-coop\A\Binaries\foc_coop.log" -Pattern "poke:" | Select-Object -Last 4 | ForEach-Object { $_.Line }

