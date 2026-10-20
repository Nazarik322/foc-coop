# B15 test: the host teleports the guest's pawn next to its own pawn with the native SetLocation (`tpg`, ini tpsync).
# Step 1: tpg 1500 0 200 sends the guest away from the host (checks that a native move works at all); step 2: tpg moves
# it back. Both the host's and the guest's own state file must show the guest within 800 uu of the host.
# -Auto: also drop the guest 5000 uu below the host (tpg 0 0 -5000) and wait for the rescue (rescue=1, tpsync=1) to bring it back.
# Twin: -Twin writes tpsync=0 into A's ini; the result must be FAIL.
# Usage: tptest.ps1 -Map L06_Megatron_BASE_m [-Twin] [-Auto]
param([string]$Map = "L06_Megatron_BASE_m", [string]$Out = "", [switch]$Twin, [switch]$Auto, [string[]]$IniExtra = @(), [string]$SlotOwner = "", [switch]$NoSlot, [string]$HostCopy = "A", [string]$GuestCopy = "B")
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
if (-not $Out) { $Out = "E:\FoC-coop\sessions\tp_$(Get-Date -Format yyyyMMdd_HHmm)_$Map" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "tp.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
$ini = "E:\FoC-coop\$HostCopy\Binaries\foc_coop.ini"
$iniOrig = Get-Content $ini -Raw
$extra = @("tpsync=1", "rescue=1") + @($IniExtra)
if ($Twin) { $extra = @("tpsync=0") + @($IniExtra); if ($Auto) { $extra += "rescue=0" } }
Set-Content $ini ($iniOrig.TrimEnd() + "`r`n" + ($extra -join "`r`n") + "`r`n") -Encoding ascii
function Dist($a, $b) { [math]::Sqrt([math]::Pow($a.x - $b.x, 2) + [math]::Pow($a.y - $b.y, 2) + [math]::Pow($a.z - $b.z, 2)) }
function Pos($copy, [bool]$local) { $s = Read-FocState $copy; $s.players | Where-Object { [bool]$_.local -eq $local -and $_.pawn } | Select-Object -First 1 }
try {
  $m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
  Say "start: $m"
  if ($m -notlike "RESULT: PASS*") { Say "RESULT: SKIP $Map cannot start"; return }
  $pa = (Find-CopyProc $HostCopy).Id; $pb = (Find-CopyProc $GuestCopy).Id
  $sa = Read-FocState $HostCopy; $sb = Read-FocState $GuestCopy
  Say "mode: hostMap=$($sa.map) hostRole=$($sa.role) guestRole=$($sb.role) hostPlayers=$(@($sa.players).Count) guestPlayers=$(@($sb.players).Count)"
  if ($sa.map -notlike "*$($Map -replace '_BASE_m','')*" -or $sa.role -ne "host" -or $sb.role -ne "client" -or @($sa.players).Count -ne 2) {
    Say "RESULT: FAIL $Map wrong mode (map/role/players)"; return }
  Start-Sleep 5
  $fails = @()
  $h = Pos $HostCopy $true; $g = Pos $HostCopy $false
  Say "start pos host $($h.x),$($h.y),$($h.z) guest $($g.x),$($g.y),$($g.z) dist $([int](Dist $h $g))"
  Send-Cmd $HostCopy "tpg 1500 0 200"; Start-Sleep 4
  $h = Pos $HostCopy $true; $g = Pos $HostCopy $false; $gb = Pos $GuestCopy $true
  $d1 = [int](Dist $h $g)
  Say "after tpg away: host-view dist $d1, guest own pos $($gb.x),$($gb.y),$($gb.z)"
  Send-Cmd $HostCopy "shot $Out\away_A.bmp"; Start-Sleep 2; Send-Cmd $GuestCopy "shot $Out\away_B.bmp"; Start-Sleep 2
  Send-Cmd $HostCopy "tpg"; Start-Sleep 5
  $h = Pos $HostCopy $true; $g = Pos $HostCopy $false; $gb = Pos $GuestCopy $true
  $d2 = [int](Dist $h $g); $d2b = [int](Dist $h $gb)
  Say "after tpg back: host-view dist $d2, guest own view dist $d2b (guest own pos $($gb.x),$($gb.y),$($gb.z))"
  Send-Cmd $HostCopy "shot $Out\back_A.bmp"; Start-Sleep 2; Send-Cmd $GuestCopy "shot $Out\back_B.bmp"; Start-Sleep 2
  if ($d1 -lt 800) { $fails += "tpg away did not move the guest (dist $d1)" }
  if ($d2 -gt 800) { $fails += "tpg back: host view dist $d2" }
  if ($d2b -gt 800) { $fails += "tpg back: guest own view dist $d2b" }
  if ($Auto) {
    Send-Cmd $HostCopy "tpg 0 0 -5000"; Say "auto: guest dropped 5000 below"
    $ok = $false
    for ($i = 0; $i -lt 45; $i++) {
      Start-Sleep 1
      $h = Pos $HostCopy $true; $g = Pos $HostCopy $false
      if ($h -and $g -and (Dist $h $g) -lt 800) { $ok = $true; break }
    }
    Say "auto: rescued=$ok after $i s"
    Send-Cmd $HostCopy "shot $Out\auto_A.bmp"; Start-Sleep 2; Send-Cmd $GuestCopy "shot $Out\auto_B.bmp"; Start-Sleep 2
    if (-not $ok) { $fails += "auto rescue did not bring the guest back" }
  }
  if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue)) { $fails += "host died" }
  if (-not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { $fails += "guest died" }
  foreach ($c in @($HostCopy, $GuestCopy)) { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  if ($fails) { Say "RESULT: FAIL $Map $($fails -join '; ')" } else { Say "RESULT: PASS $Map guest teleported away ($d1) and back (host view $d2, guest view $d2b)" }
} finally {
  Set-Content $ini $iniOrig -NoNewline -Encoding ascii
  Stop-Foc; Stop-Relay
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
