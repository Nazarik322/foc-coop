# B4 test: open a listen session and join it inside the running games (no relaunch), with the front end focused.
# Host (A): main menu, window focused, `open <map>?listen` through foc_cmd.txt; guest (B): main menu, focused,
# `open 127.0.0.1:<port>`. PASS when both reach the map (state file map/role) and neither is back in the front end
# 40 s later. -Twin: fakelogin=0 (the old behaviour: back to the front end, "You must be signed in to play online").
param([string]$Map = "L01_TheExodus_BASE_m", [string]$Out = "E:\FoC-coop\sessions\b4", [switch]$Twin, [switch]$NoSlot, [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
if (-not $NoSlot) { Enter-FocSlot -Owner (Get-FocSlotOwner "") }
New-Item -ItemType Directory -Force $Out | Out-Null
$inis = @{}
foreach ($c in "A", "B") { $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"; $inis[$p] = Get-Content $p -Raw; Set-Content $p ($inis[$p].TrimEnd() + "`r`nfakelogin=$(if ($Twin) { 0 } else { 1 })`r`n") -Encoding ascii }
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "b4.txt") $l; $l }
try {
  Stop-Foc; Stop-Relay; Clear-FocState A; Clear-FocState B
  $pa = Start-Foc "A" "" 960 540; $ha = Wait-FocWindow $pa 90
  Start-Sleep 25; FocusWin $ha; Start-Sleep 3
  Send-Cmd A "open $Map`?Game=TransContent.TnCampaignGame`?listen`?MaxPlayers=2"
  Say "host: open sent (focused)"
  $sa = Wait-FocState A { param($s) $s.map -eq $Map -and $s.role -eq "host" -and $s.port -gt 0 } 120
  if (-not $sa) { Say "RESULT: FAIL host did not reach $Map as a listen host"; return }
  Say "host in $Map, port $($sa.port)"
  $pb = Start-Foc "B" "" 960 540; $hb = Wait-FocWindow $pb 90
  Start-Sleep 25; FocusWin $hb; Start-Sleep 3
  Send-Cmd B "open 127.0.0.1:$($sa.port)"
  Say "guest: open sent (focused)"
  $sb = Wait-FocState B { param($s) $s.map -eq $Map -and $s.role -eq "client" } 150
  if (-not $sb) { Say "RESULT: FAIL guest did not reach $Map"; return }
  Say "guest in $Map"
  Start-Sleep 40
  $a = Read-FocState A; $b = Read-FocState B
  Say "after 40 s: host map=$($a.map) role=$($a.role) players=$(@($a.players).Count); guest map=$($b.map) role=$($b.role)"
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  $fl = @(Select-String -Path (Join-Path $Out "A.log"), (Join-Path $Out "B.log") -Pattern "fakelogin:" | ForEach-Object { $_.Line }); $fl | Select-Object -First 4 | ForEach-Object { Say $_ }
  if ($a.map -eq $Map -and $b.map -eq $Map -and $b.role -eq "client" -and @($a.players).Count -ge 2) { Say "RESULT: PASS no relaunch: host and guest stay in $Map" }
  else { Say "RESULT: FAIL host $($a.map)/$(@($a.players).Count) players, guest $($b.map)/$($b.role)" }
} finally {
  foreach ($p in $inis.Keys) { Set-Content $p $inis[$p] -NoNewline -Encoding ascii }
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  if (-not $NoSlot) { Exit-FocSlot -Owner (Get-FocSlotOwner "") }
}
