# Escalation through the game's own menus with up to 4 copies (owner 2026-10-21/22): A hosts, B/C/D are friends.
# Every copy opens ESCALATION (party lobby); A invites the others with Steam lobby invites (mock Steam: accepted at
# once, ini escautoaccept=1 in B/C/D); A: Private Match -> Create Game (everyone in the game lobby) -> Start Game
# (everyone loads the map through the Steam relay). No player actions. Windows 800x450 in a 2x2 grid; screenshots of
# all copies after each step in $Out. Invented names (ini playername) so the lobby lists are readable.
param([string[]]$Copies = @("A", "B", "C", "D"), [string]$Out = "E:\FoC-coop\sessions\esc4menu", [switch]$NoBuild, [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "esc4menu.txt") $l; $l }
$names = @{ A = "Optimus"; B = "Bumblebee"; C = "Ironhide"; D = "Ratchet" }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" })
  if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
function Log($c) { "E:\FoC-coop\$c\Binaries\foc_coop.log" }
function Shot($c, $tag) {
  $f = "$Out\$tag`_$c.bmp"; Send-Cmd $c "shot $f"; Start-Sleep 3
  if (Test-Path $f) { $i = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 960, 540; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($i, 0, 0, 960, 540); $b.Save("$Out\$tag`_$c.png", [Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $b.Dispose(); $i.Dispose(); Remove-Item $f }
}
function Keys($c, [string[]]$k) { foreach ($x in $k) { if ($x -like "WAIT*") { Start-Sleep ([int]$x.Substring(4)); continue }; Send-Cmd $c "input hold $x 20"; Start-Sleep 2 } }
function State($c) { $m = Select-String -Path (Log $c) -Pattern "esclogin: game (\S+) (\S+)" | Select-Object -Last 1; if ($m) { "$($m.Matches[0].Groups[1].Value) $($m.Matches[0].Groups[2].Value)" } else { "-" } }
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  foreach ($c in $Copies) {
    Set-IniKey $c "playername" $names[$c]; Set-IniKey $c "escautoaccept" ([int]($c -ne "A"))
    if ($c -ne "A") { Set-IniKey $c "instance" $c }
  }
  if (-not $NoBuild) { Build-Deploy -Repo (Split-Path -Parent $PSScriptRoot) -Copies $Copies -NoSlot | Out-Null; Deploy-Relay -Copies $Copies -NoSlot | Out-Null }
  Stop-Foc; Stop-Relay; Get-Process FoCRelay -ErrorAction SilentlyContinue | Stop-Process -Force
  foreach ($c in $Copies) { [void](Start-Foc $c "" 800 450); Start-Sleep 25 }
  foreach ($c in $Copies) { for ($i = 0; $i -lt 240; $i++) { if (Select-String -Path (Log $c) -Pattern "overlay initialised" -Quiet) { break }; Start-Sleep 1 } }
  Start-Sleep 15
  foreach ($c in $Copies) { Keys $c @("DOWN", "DOWN", "ENTER") }
  Start-Sleep 35
  $ids = @{}
  foreach ($c in $Copies) { $m = Select-String -Path (Log $c) -Pattern "steamnet: event hello (\d+)" | Select-Object -Last 1; if ($m) { $ids[$c] = $m.Matches[0].Groups[1].Value } }
  foreach ($c in $Copies) { Say "$c ($($names[$c])) Steam id $($ids[$c]) state $(State $c)" }
  foreach ($c in $Copies) { Shot $c "1_party_alone" }
  foreach ($c in $Copies | Where-Object { $_ -ne "A" }) { Send-Cmd "A" "escinvite $($ids[$c])"; Start-Sleep 20 }
  Start-Sleep 15
  foreach ($c in $Copies) { Say "after invites: $c state $(State $c)"; Shot $c "2_party_together" }
  Keys "A" @("DOWN", "ENTER", "WAIT6", "ENTER", "WAIT35")
  foreach ($c in $Copies) { Say "after Create Game: $c state $(State $c)"; Shot $c "3_game_lobby" }
  Keys "A" @("ENTER", "WAIT75")
  foreach ($c in $Copies) { Shot $c "4_map" }
  $n = Select-String -Path (Log "A") -Pattern "stat: npc=(\d+)" | Select-Object -Last 1
  $npc = if ($n) { [int]$n.Matches[0].Groups[1].Value } else { 0 }
  Say "host sees $npc player controller(s)"
  if ($npc -ge $Copies.Count) { Say "RESULT: PASS $($Copies.Count) players through the Escalation menus" } else { Say "RESULT: FAIL host sees $npc of $($Copies.Count) players" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
