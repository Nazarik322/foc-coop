# Lobby test (owner 2026-10-20): no map before the start, no restarts. A creates a lobby from the main menu
# (`lobby create`), B joins it (`lobby join <id>`, id from A's relay.status) and stays in the main menu, then A starts the
# mission the way the game's own menu does (`lobby menustart <map>` through the hooked console Exec). PASS when B
# follows into the map by itself and both stay there 30 s, with the same processes (no relaunch). Mock Steam (test copies).
param([string]$Map = "L01_TheExodus_BASE_m", [string]$Out = "E:\FoC-coop\sessions\lobby", [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
Enter-FocSlot -Owner (Get-FocSlotOwner "")
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "lobby.txt") $l; $l }
function RelayStatus($c) { $f = "E:\FoC-coop\$c\Binaries\foc_relay\relay.status"; $h = @{}; if (Test-Path $f) { foreach ($l in Get-Content $f) { if ($l -match '^(\w+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } } }; $h }
try {
  Stop-Foc; Stop-Relay; Clear-FocState A; Clear-FocState B
  $pa = Start-Foc "A" "" 960 540; [void](Wait-FocWindow $pa 90)
  $pb = Start-Foc "B" "" 960 540; [void](Wait-FocWindow $pb 90)
  Start-Sleep 30
  Send-Cmd A "lobby create 4"
  $id = $null
  for ($i = 0; $i -lt 30 -and -not $id; $i++) { Start-Sleep 1; $r = RelayStatus A; if ($r.state -like "hosting*" -and $r.lobby -and $r.lobby -ne "0") { $id = $r.lobby } }
  if (-not $id) { Say "RESULT: FAIL host lobby not created ($((RelayStatus A).state))"; return }
  Say "host lobby $id"
  Send-Cmd B "lobby join $id"
  $ok = $false
  for ($i = 0; $i -lt 30 -and -not $ok; $i++) { Start-Sleep 1; $r = RelayStatus B; if ($r.state -like "joined*" -or $r.state -like "connected*") { $ok = $true } }
  if (-not $ok) { Say "RESULT: FAIL guest did not enter the lobby ($((RelayStatus B).state))"; return }
  $sb = Read-FocState B
  Say "guest in lobby, guest map=$($sb.map) (main menu expected)"
  Start-Sleep 3
  Send-Cmd A "lobby menustart $Map"
  Say "host: mission started from the 'game menu'"
  $sa = Wait-FocState A { param($s) $s.map -eq $Map -and $s.role -eq "host" -and $s.port -gt 0 } 120
  if (-not $sa) { Say "RESULT: FAIL host not hosting $Map"; return }
  Say "host in $Map port $($sa.port), relay go: $((RelayStatus A).go)"
  $sb = Wait-FocState B { param($s) $s.map -eq $Map -and $s.role -eq "client" } 180
  if (-not $sb) { Say "RESULT: FAIL guest did not follow into $Map (relay go=$((RelayStatus B).go) state=$((RelayStatus B).state))"; return }
  Say "guest followed into $Map"
  Start-Sleep 30
  $a = Read-FocState A; $b = Read-FocState B
  $same = (Get-Process -Id $pa -ErrorAction SilentlyContinue) -and (Get-Process -Id $pb -ErrorAction SilentlyContinue)
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  if ($same -and $a.map -eq $Map -and $b.map -eq $Map -and @($a.players).Count -ge 2) { Say "RESULT: PASS lobby -> mission start -> guest followed, no relaunch ($(@($a.players).Count) players)" }
  else { Say "RESULT: FAIL after 30 s host $($a.map)/$(@($a.players).Count) guest $($b.map) same processes $same" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
