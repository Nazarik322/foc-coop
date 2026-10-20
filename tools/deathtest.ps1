# B11 test: in a story chapter one player dies (dropped below the level with the `fall` command, like a fall into a
# pit) while the other lives. Expected: no game over (host state gameOverSkipped grows), the dead player gets a new pawn
# within 40 s, both games keep running. First the guest dies, then the host.
# Twin: -Twin writes coopdeath=0 into A's ini; then the result must be FAIL.
# Usage: deathtest.ps1 -Map L06_Megatron_BASE_m [-Twin]
param([string]$Map = "L06_Megatron_BASE_m", [string]$Out = "", [switch]$Twin, [string[]]$IniExtra = @(), [string]$SlotOwner = "", [switch]$NoSlot)
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
if (-not $Out) { $Out = "E:\FoC-coop\sessions\death_$(Get-Date -Format yyyyMMdd_HHmm)_$Map" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "death.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
$ini = "E:\FoC-coop\A\Binaries\foc_coop.ini"
$iniOrig = Get-Content $ini -Raw
$extra = @($IniExtra); if ($Twin) { $extra += "coopdeath=0" }
if ($extra.Count) { Set-Content $ini ($iniOrig.TrimEnd() + "`r`n" + ($extra -join "`r`n") + "`r`n") -Encoding ascii }
try {
  $m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
  Say "start: $m"
  if ($m -notlike "RESULT: PASS*") { Say "RESULT: SKIP $Map cannot start"; return }
  $pa = (Find-CopyProc A).Id; $pb = (Find-CopyProc B).Id
  $fails = @()
  foreach ($who in "remote", "local") {
    $before = Read-FocState A
    $skip0 = [int]$before.gameOverSkipped
    Send-Cmd A "fall $who"
    Say "fall $who"
    Start-Sleep 6
    Send-Cmd A "shot $Out\$who`_after_A.bmp"; Start-Sleep 2; Send-Cmd B "shot $Out\$who`_after_B.bmp"
    $ok = $false
    for ($i = 0; $i -lt 40; $i++) {
      Start-Sleep 1
      if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue) -or -not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { break }
      $s = Read-FocState A
      $p = $s.players | Where-Object { [bool]$_.local -eq ($who -eq "local") }
      if ($p -and $p.pawn -and ([int]$s.gameOverSkipped -gt $skip0)) { $ok = $true; break }
    }
    $s = Read-FocState A
    $p = $s.players | Where-Object { [bool]$_.local -eq ($who -eq "local") }
    Say "after $who death: skipped=$($s.gameOverSkipped) respawns=$($s.respawns) pawn=$($p.pawn) at $($p.x),$($p.y),$($p.z) state=$($p.state)"
    Start-Sleep 3
    Send-Cmd A "shot $Out\$who`_respawn_A.bmp"; Start-Sleep 2; Send-Cmd B "shot $Out\$who`_respawn_B.bmp"; Start-Sleep 2
    if (-not $ok) { $fails += "$who player: no respawn without game over" }
  }
  if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue)) { $fails += "host died" }
  if (-not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { $fails += "guest died" }
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  if ($fails) { Say "RESULT: FAIL $Map $($fails -join '; ')" } else { Say "RESULT: PASS $Map both deaths respawned without game over" }
} finally {
  Set-Content $ini $iniOrig -NoNewline -Encoding ascii
  Stop-Foc; Stop-Relay
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
