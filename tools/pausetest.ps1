# Pause menu test (2026-10-21: the host crashed - call to address 0 - when Esc opened the pause menu in a session it
# had opened inside the running game). Host A only: main menu, `open L01...?listen` through foc_cmd.txt, 40 s in the
# map, Esc, 8 s, Esc again; PASS when the process lives and the log has no EXCEPTION at address 0.
# -Ini: extra ini lines for the run, e.g. "exechook=0" or "fakelogin=0" (restored afterwards).
# -Guest: copy B joins before the Esc; -Checkpoint: the URL options of the menu's CONTINUE (?LoadCheckpoint ...)
param([string[]]$Ini = @(), [string]$Out = "E:\FoC-coop\sessions\pause", [switch]$Guest, [switch]$Checkpoint)
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
$iniPath = "E:\FoC-coop\A\Binaries\foc_coop.ini"; $orig = Get-Content $iniPath -Raw
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "pause.txt") $l; $l }
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  if ($Ini.Count) { Set-Content $iniPath ($orig.TrimEnd() + "`r`n" + ($Ini -join "`r`n") + "`r`n") -Encoding ascii }
  Stop-Foc; Stop-Relay; Clear-FocState A
  $pa = Start-Foc "A" "" 960 540; [void](Wait-FocWindow $pa 90)
  for ($i = 0; $i -lt 240; $i++) { Start-Sleep 1; if (Select-String -Path E:\FoC-coop\A\Binaries\foc_coop.log -Pattern "overlay initialised" -Quiet) { break } }
  Start-Sleep 8
  $opts = if ($Checkpoint) { "?PlaylistId=0?GamerRegion=0?GameMode=0?GameRules=?GameTeamStatus=4?Name=Player?Team=255?LoadCheckpoint" } else { "" }
  Send-Cmd A "open L01_TheExodus_BASE_m?Game=TransContent.TnCampaignGame$opts?listen?MaxPlayers=2"
  $s = Wait-FocState A { param($s) $s.map -eq "L01_TheExodus_BASE_m" } 150
  if (-not $s) { Say "RESULT: FAIL host did not reach L01 ($($Ini -join ','))"; return }
  if ($Guest) {
    $sa = Wait-FocState A { param($s) $s.port -gt 0 } 60
    $pb = Start-Foc "B" "" 960 540; [void](Wait-FocWindow $pb 90)
    for ($i = 0; $i -lt 240; $i++) { Start-Sleep 1; if (Select-String -Path E:\FoC-coop\B\Binaries\foc_coop.log -Pattern "overlay initialised" -Quiet) { break } }
    Start-Sleep 8; Send-Cmd B "open 127.0.0.1:$($sa.port)"
    [void](Wait-FocState B { param($s) $s.map -eq "L01_TheExodus_BASE_m" } 150)
    Say "guest in: $((Read-FocState B).map)"
  }
  Start-Sleep 40
  Send-Cmd A "input tap ESC"; Start-Sleep 8; Send-Cmd A "input tap ESC"; Start-Sleep 6
  $alive = [bool](Get-Process -Id $pa -ErrorAction SilentlyContinue)
  $null0 = @(Select-String -Path E:\FoC-coop\A\Binaries\foc_coop.log -Pattern "EXCEPTION c0000005 at 00000000").Count
  Copy-Item E:\FoC-coop\A\Binaries\foc_coop.log (Join-Path $Out ("A_" + (($Ini -join '_') -replace '[^\w]', '') + ".log")) -ErrorAction SilentlyContinue
  if ($alive -and $null0 -eq 0) { Say "RESULT: PASS pause menu ($($Ini -join ','))" } else { Say "RESULT: FAIL pause menu: alive $alive, null calls $null0 ($($Ini -join ','))" }
} finally {
  Set-Content $iniPath $orig -NoNewline -Encoding ascii
  Stop-Foc; Stop-Relay
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
