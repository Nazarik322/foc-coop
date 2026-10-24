# Owner's real 2-PC session 2026-10-24 replayed with two copies (mock Steam), keys only for the game menus:
#  1. no Escalation svc relay in the main menu (it starts only from ESCALATION),
#  2. A creates an F10 lobby, B joins it; banner screenshots (top left) on both,
#  3. A opens the Join page: its own lobby is not listed and Join is blocked while hosting (screenshot, Russian text),
#  4. B (the guest) presses CONTINUE itself: B must not wait for guests (no pause),
#  5. A presses CONTINUE: B must end up as A's client on the same map,
#  6. A closes the lobby: B's relay must stop with "host left the lobby" and B's lobby role is cleared.
param([string]$Out = "E:\FoC-coop\sessions\lobbyfriend", [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Log($c) { "E:\FoC-coop\$c\Binaries\foc_coop.log" }
function Shot($c, $tag, [switch]$Ui) {
  $f = "$Out\$tag`_$c.bmp"; Send-Cmd $c "$(if ($Ui) { 'shotui' } else { 'shot' }) $f"; Start-Sleep 3
  if (Test-Path $f) { $i = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 960, 540; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($i, 0, 0, 960, 540); $b.Save("$Out\$tag`_$c.png", [Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $b.Dispose(); $i.Dispose(); Remove-Item $f }
}
function Status($c) { $h = @{}; $f = "E:\FoC-coop\$c\Binaries\foc_relay\relay.status"; if (Test-Path $f) { foreach ($l in Get-Content $f -Encoding UTF8) { if ($l -match '^(\w+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } } }; $h }
function Has($c, $pat) { [bool](Select-String -Path (Log $c) -Pattern $pat -Quiet) }
$fail = @()
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  Stop-Foc; Stop-Relay; Get-Process FoCRelay -ErrorAction SilentlyContinue | Stop-Process -Force
  foreach ($c in "A", "B") { Clear-FocState $c; Remove-Item "E:\FoC-coop\$c\Binaries\foc_relay\relay.status", "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt", "E:\FoC-coop\$c\Binaries\foc_cmd.txt" -ErrorAction SilentlyContinue }
  $pa = Start-Foc "A" "" 960 540; [void](Wait-FocWindow $pa 90); Start-Sleep 25
  $pb = Start-Foc "B" "" 960 540; [void](Wait-FocWindow $pb 90)
  foreach ($c in "A", "B") { $ok = $false; for ($i = 0; $i -lt 240 -and -not $ok; $i++) { Start-Sleep 1; $ok = Has $c "overlay initialised" }; if (-not $ok) { Say "RESULT: SKIP $c has no game window (B7, remote desktop not shown?)"; return } }
  Start-Sleep 25
  # 1
  foreach ($c in "A", "B") { if (Has $c 'FoCRelay.exe" svc') { $fail += "$c started the svc relay in the main menu" } }
  Say "1 svc relay in the main menu: A $(Has A 'FoCRelay.exe" svc') B $(Has B 'FoCRelay.exe" svc')"
  # 2
  Send-Cmd A "lobby create 4"
  $id = $null; for ($i = 0; $i -lt 30 -and -not $id; $i++) { Start-Sleep 1; $s = Status A; if ($s.state -like "hosting*" -and $s.lobby -and $s.lobby -ne "0") { $id = $s.lobby } }
  if (-not $id) { Say "RESULT: FAIL host lobby not created"; return }
  Send-Cmd B "lobby join $id"
  $ok = $false; for ($i = 0; $i -lt 30 -and -not $ok; $i++) { Start-Sleep 1; $s = Status B; $ok = $s.state -like "joined*" -or $s.state -like "connected*" }
  if (-not $ok) { Say "RESULT: FAIL guest did not enter the lobby ($((Status B).state))"; return }
  Start-Sleep 4
  $ma = @((Get-Content "E:\FoC-coop\A\Binaries\foc_relay\relay.status" -Encoding UTF8) -match '^member=').Count
  Say "2 lobby ${id}: A members $ma, B state $((Status B).state)"
  if ($ma -lt 2) { $fail += "host relay lists $ma members" }
  Shot A "2_banner" -Ui; Shot B "2_banner" -Ui
  # 3
  Send-Cmd A "menu 2"; Start-Sleep 10
  $own = (Test-Path "E:\FoC-coop\A\Binaries\foc_relay\lobbies.txt") -and [bool](Select-String -Path "E:\FoC-coop\A\Binaries\foc_relay\lobbies.txt" -Pattern "^$id\|" -Quiet)
  Say "3 host's Join page lists its own lobby: $own"
  if ($own) { $fail += "own lobby listed" }
  Shot A "3_joinpage" -Ui; Send-Cmd A "menu off"; Start-Sleep 2
  # 4
  Send-Cmd B "input tap ENTER"; Start-Sleep 5; Send-Cmd B "input tap ENTER"; Start-Sleep 35   # B has no save: CAMPAIGN > chapter I
  $sb = Read-FocState B
  $waited = Has B "waitguest: .* loaded, pausing"; $unp = Has B "we are a lobby guest"
  Say "4 guest pressed CONTINUE: B map $($sb.map) role $($sb.role); waitguest started $waited, released as guest $unp"
  if ($waited -and -not $unp) { $fail += "guest waits for guests" }
  Shot B "4_guest_continue" -Ui
  # 5
  Send-Cmd A "input tap UP"; Start-Sleep 3; Send-Cmd A "input tap ENTER"
  $sa = Wait-FocState A { param($s) $s.role -eq "host" -and $s.port -gt 0 -and $s.map -like "L*" } 150
  if (-not $sa) { Say "RESULT: FAIL host not hosting after CONTINUE"; return }
  $sb = Wait-FocState B { param($s) $s.role -eq "client" -and $s.map -eq $sa.map } 150
  Say "5 host $($sa.map) port $($sa.port); guest $(if ($sb) { "client on $($sb.map)" } else { 'NOT on the host' })"
  if (-not $sb) { $fail += "guest did not reach the host's map" }
  Start-Sleep 20
  $a = Read-FocState A; Say "5b host players $(@($a.players).Count)"
  if (@($a.players).Count -lt 2) { $fail += "host sees $(@($a.players).Count) players" }
  Shot A "5_game"; Shot B "5_game"
  # 6
  Send-Cmd A "lobby leave"; Start-Sleep 35
  $sb6 = Status B
  $gone = Has B "lobby: relay gone"
  Say "6 host left: B relay state '$($sb6.state)', B lobby closed $gone"
  if (-not $gone) { $fail += "guest still in the lobby after the host left" }
  Shot B "6_after_host_left" -Ui
  foreach ($c in "A", "B") { Copy-Item (Log $c) "$Out\$c.log" -ErrorAction SilentlyContinue; Copy-Item "E:\FoC-coop\$c\Binaries\foc_relay\relay.log" "$Out\${c}_relay.log" -ErrorAction SilentlyContinue }
  if ($fail.Count) { Say "RESULT: FAIL $($fail -join '; ')" } else { Say "RESULT: PASS lobby, own lobby hidden, guest CONTINUE no wait, guest follows host CONTINUE, host leave closes guest lobby" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
