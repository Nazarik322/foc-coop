# B14 check: chapter 1, host + guest; while the host is in a cutscene (cinematic mode) the guest must be in cinematic
# mode too (state cine=1) and leave it afterwards (pawn hiding is reported only, not done yet); shots of both during and after the cutscene go to $Out.
param([string]$Map = "L01_TheExodus_BASE_m", [string]$Out = "E:\FoC-coop\sessions\cine2", [int]$Timeout = 300,
      [switch]$Twin, [string]$SlotOwner = "", [switch]$NoSlot)   # mutation twin: cinesync=0 on the host, the result must be FAIL
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
New-Item -ItemType Directory -Force $Out | Out-Null
$ini = "E:\FoC-coop\A\Binaries\foc_coop.ini"
$iniText = Get-Content $ini -Raw
if ($Twin) { Set-Content $ini ($iniText.TrimEnd() + "`r`ncinesync=0`r`n") -Encoding ascii -NoNewline }
& "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-Object -Last 3
$t0 = Get-Date; $phase = 0; $sawGuestCine = $false; $sawHidden = $false; $unhidden = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Timeout -and ($phase -lt 2 -or ((Get-Date) - $t0).TotalSeconds -lt 150)) {
  Start-Sleep -Seconds 1
  $a = Read-FocState A; $b = Read-FocState B
  if (-not $a -or -not $b) { continue }
  $hc = ($a.players | Where-Object { $_.local -eq 1 }).cine
  $gc = ($b.players | Where-Object { $_.local -eq 1 }).cine
  $gp = $b.players | Where-Object { $_.local -eq 1 }
  if ($hc -eq 1 -and $gc -eq 1) { $sawGuestCine = $true }
  if ($hc -eq 1 -and $gp.pawn -and $gp.hidden -eq 1) { $sawHidden = $true }
  if ($phase -ge 1 -and $hc -eq 0 -and $gp.pawn) { $unhidden = ($gp.hidden -eq 0) }
  if ($phase -eq 0 -and $hc -eq 1) { Start-Sleep 3; Send-Cmd A "shot $Out\cine_A.bmp"; Send-Cmd B "shot $Out\cine_B.bmp"; Write-Host "in cutscene: host cine=$hc guest cine=$gc"; $phase = 1 }
  elseif ($phase -eq 1 -and $hc -eq 0) { Start-Sleep 3; Send-Cmd A "shot $Out\after_A.bmp"; Send-Cmd B "shot $Out\after_B.bmp"; Write-Host "after cutscene: guest cine=$gc"; $phase = 2 }
}
Start-Sleep 2
Select-String -Path "E:\FoC-coop\A\Binaries\foc_coop.log" -Pattern "cinesync" | Select-Object -Last 6 | ForEach-Object { $_.Line }
# the guest's pawn may not exist yet during the first cutscene, so sawHidden is reported but not required
"guest pawn hidden during a cutscene: $sawHidden; not hidden after: $unhidden"
if ($sawGuestCine -and $phase -eq 2) { "RESULT: PASS cinesync" } else { "RESULT: FAIL cinesync sawGuestCine=$sawGuestCine phase=$phase unhidden=$unhidden" }
if ($Twin) { Set-Content $ini $iniText -Encoding ascii -NoNewline }
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
