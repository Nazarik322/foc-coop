# Owner 2026-10-25: "test it like 2-4 real people: one creates the lobby in F10, the others also press and join - all
# visually, not with commands, and fast". Only menu clicks and keys: F10 (key), F10 menu buttons (mouse click on the
# button by its label: `input uiclick`, the menu's cursor is moved there), the game's menu (keys). No walking.
#   host: F10 -> Host a game -> Create lobby;  guests: F10 -> Join a game -> Join lobby (first row);
#   host: CONTINUE in the game menu (UP, ENTER); everybody must end up in the host's mission.
# Screenshots with the overlay (shotui) of every copy after each step, as 2x2 grids in $Out.
param([string[]]$Copies = @("A", "B"), [string]$Out = "E:\FoC-coop\sessions\f10visual", [switch]$Keep, [string]$HostKeys = "UP,ENTER")
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$names = @{ A = "Optimus"; B = "Bumblebee"; C = "Ironhide"; D = "Ratchet" }
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Log($c) { "E:\FoC-coop\$c\Binaries\foc_coop.log" }
function Has($c, $pat) { [bool](Select-String -Path (Log $c) -Pattern $pat -Quiet) }
function Status($c) { $h = @{}; $f = "E:\FoC-coop\$c\Binaries\foc_relay\relay.status"; if (Test-Path $f) { foreach ($l in Get-Content $f -Encoding UTF8) { if ($l -match '^(\w+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } } }; $h }
function Key($c, $k) { Send-Cmd $c "input hold $k 150"; Start-Sleep -Milliseconds 900 }
function Click($c, $label) { Send-Cmd $c "input uiclick $label"; Start-Sleep -Milliseconds 1500 }
function Grid($tag) {
  foreach ($c in $Copies) { Send-Cmd $c "shotui $Out\$tag`_$c.bmp" }; Start-Sleep 3
  $g = New-Object Drawing.Bitmap 1920, 1080; $gr = [Drawing.Graphics]::FromImage($g); $i = 0
  foreach ($c in $Copies) { $f = "$Out\$tag`_$c.bmp"; if (Test-Path $f) { $im = [Drawing.Image]::FromFile($f); $gr.DrawImage($im, ($i % 2) * 960, [int][Math]::Floor($i / 2) * 540, 960, 540); $im.Dispose(); Remove-Item $f }; $i++ }
  $g.Save("$Out\$tag.png", [Drawing.Imaging.ImageFormat]::Png); $gr.Dispose(); $g.Dispose()
}
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" }); if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
$fail = @(); $hostC = $Copies[0]; $guests = @($Copies | Select-Object -Skip 1)
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  foreach ($c in $Copies) { Set-IniKey $c "playername" $names[$c]; if ($c -ne "A") { Set-IniKey $c "instance" $c } }
  Stop-Foc; Stop-Relay; Get-Process FoCRelay -ErrorAction SilentlyContinue | Stop-Process -Force
  foreach ($c in $Copies) { Clear-FocState $c; Remove-Item "E:\FoC-coop\$c\Binaries\foc_relay\relay.status", "E:\FoC-coop\$c\Binaries\foc_cmd.txt" -ErrorAction SilentlyContinue }
  foreach ($c in $Copies) { [void](Start-Foc $c "" 1280 720); Start-Sleep 20 }   # 1280x720: 800x450 cut the F10 menu (Create lobby below the edge)
  foreach ($c in $Copies) { $ok = $false; for ($i = 0; $i -lt 180 -and -not $ok; $i++) { Start-Sleep 1; $ok = Has $c "overlay initialised" }; if (-not $ok) { Say "RESULT: SKIP $c has no game window (B7)"; return } }
  Start-Sleep 12
  # host creates the lobby
  Key $hostC "F10"; Click $hostC "Host a game"; Grid "1_host_page"; Click $hostC "Create lobby"
  $id = $null; for ($i = 0; $i -lt 20 -and -not $id; $i++) { Start-Sleep 1; $s = Status $hostC; if ($s.state -like "hosting*" -and $s.lobby -and $s.lobby -ne "0") { $id = $s.lobby } }
  Say "host lobby: $(if ($id) { $id } else { 'NOT created' })"; if (-not $id) { $fail += "host lobby not created by the menu" }
  # guests join from their Join page
  foreach ($c in $guests) {
    Key $c "F10"; Click $c "Join a game"
    $listed = $false; for ($i = 0; $i -lt 20 -and -not $listed; $i++) { Start-Sleep 1; $listed = (Test-Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt") -and [bool](Select-String -Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt" -Pattern "^$id\|" -Quiet) }
    Start-Sleep 2; Grid "2_join_page_$c"
    Click $c "Join lobby"
    $ok = $false; for ($i = 0; $i -lt 20 -and -not $ok; $i++) { Start-Sleep 1; $s = Status $c; $ok = $s.state -like "joined*" -or $s.state -like "connected*" }
    Say "${c}: listed $listed, in the lobby $ok ($((Status $c).state))"; if (-not $ok) { $fail += "$c did not join through the menu" }
  }
  Start-Sleep 3; Grid "3_lobby_banners"
  # host starts the campaign from the game's own menu
  foreach ($k in $HostKeys -split ",") { Key $hostC $k; Start-Sleep 4 }
  $sa = Wait-FocState $hostC { param($s) $s.role -eq "host" -and $s.port -gt 0 -and $s.map -like "L*" } 150
  if (-not $sa) { $fail += "host did not start a mission"; Say "host not in a mission" }
  else {
    Say "host in $($sa.map)"
    foreach ($c in $guests) { $sb = Wait-FocState $c { param($s) $s.role -eq "client" -and $s.map -eq $sa.map } 150; Say "${c}: $(if ($sb) { "client on $($sb.map)" } else { 'NOT in the mission' })"; if (-not $sb) { $fail += "$c not in the mission" } }
    Start-Sleep 15; Grid "4_in_game"
    $n = @((Read-FocState $hostC).players).Count; Say "host sees $n players"; if ($n -lt $Copies.Count) { $fail += "host sees $n of $($Copies.Count) players" }
  }
  foreach ($c in $Copies) { Copy-Item (Log $c) "$Out\$c.log" -ErrorAction SilentlyContinue }
  if ($fail.Count) { Say "RESULT: FAIL $($fail -join '; ')" } else { Say "RESULT: PASS $($Copies.Count) players joined through the F10 menu by clicks and started the mission" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
