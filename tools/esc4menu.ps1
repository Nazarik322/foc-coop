# Escalation through the game's own menus with up to 4 copies (owner 2026-10-21/22): A hosts, B/C/D are friends.
# Every copy opens ESCALATION (party lobby); A invites the others with Steam lobby invites (mock Steam: accepted at
# once, ini escautoaccept=1 in B/C/D); A: Private Match -> Create Game (everyone in the game lobby) -> Start Game
# (everyone loads the map through the Steam relay). No player actions. Windows 800x450 in a 2x2 grid; screenshots of
# all copies after each step in $Out. Invented names (ini playername) so the lobby lists are readable.
param([string[]]$Copies = @("A", "B", "C", "D"), [string]$Out = "E:\FoC-coop\sessions\esc4menu", [switch]$NoBuild, [switch]$Keep, [switch]$Probe, [int]$LobbyWait = 0, [switch]$Play, [int]$Rounds = 3, [int]$MapSteps = 0, [switch]$MenuInvite)
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
  # -MenuInvite (2026-10-23, B39): the game's own SendGameInvite (ONLINE FRIENDS > Send Invite) instead of the test command
  foreach ($c in $Copies | Where-Object { $_ -ne "A" }) {
    if ($MenuInvite) { Send-Cmd "A" "call cls:TnOnlineActionScriptBinding SendGameInvite s:$($ids[$c])" } else { Send-Cmd "A" "escinvite $($ids[$c])" }
    Start-Sleep 20
  }
  Start-Sleep 15
  foreach ($c in $Copies) { Say "after invites: $c state $(State $c)"; Shot $c "2_party_together"; Send-Cmd $c "sessions" }
  if ($Probe) {   # registrant dumps (heartbeat times) every 3 s while the game lobby forms
    Keys "A" @("DOWN", "ENTER", "WAIT6", "ENTER")
    for ($i = 0; $i -lt 10; $i++) { foreach ($c in $Copies) { Send-Cmd $c "sessions" }; Start-Sleep 3 }
  } else { Keys "A" @("DOWN", "ENTER", "WAIT6", "ENTER", "WAIT35") }
  foreach ($c in $Copies) { Say "after Create Game: $c state $(State $c)"; Shot $c "3_game_lobby"; Send-Cmd $c "sessions" }
  if ($LobbyWait -gt 0) { Say "waiting $LobbyWait s in the game lobby"; Start-Sleep $LobbyWait; foreach ($c in $Copies) { Say "after lobby wait: $c state $(State $c)" } }
  if ($MapSteps -gt 0) {   # 2026-10-23: Select Map (second item) RIGHT x n, back UP to Start Game; covers every ESC map
    $k = @("DOWN"); for ($j = 0; $j -lt $MapSteps; $j++) { $k += "RIGHT"; $k += "WAIT2" }; $k += "UP"; Keys "A" $k; Shot "A" "3b_map_selected"
  }
  Keys "A" @("ENTER", "WAIT75")
  foreach ($c in $Copies) { Shot $c "4_map" }
  if ($Play) {
    # owner 2026-10-22: enter the game and check that every player sees about the same screen and all names.
    # Each copy picks another character (Megatron, Shockwave, Soundwave, Starscream: RIGHT x index, ENTER); then
    # rounds of screenshots of all copies, the TAB board of every copy, and the player names each copy knows.
    $i = 0
    foreach ($c in $Copies) { $k = @(); for ($j = 0; $j -lt $i; $j++) { $k += "RIGHT" }; $k += "ENTER"; Keys $c $k; $i++ }
    Start-Sleep 30
    for ($r = 1; $r -le $Rounds; $r++) { foreach ($c in $Copies) { Shot $c "5_game_r$r" }; Start-Sleep 8 }
    for ($r = 1; $r -le $Rounds; $r++) { foreach ($c in $Copies) { Send-Cmd $c "input hold TAB 4500"; Start-Sleep 2; Shot $c "6_tab_r$r" }; Start-Sleep 3 }
    $want = @($Copies | ForEach-Object { $names[$_] })
    $allOk = $true
    foreach ($c in $Copies) {
      $st = Read-FocState $c
      $got = @($st.players | ForEach-Object { $_.name })
      $missing = @($want | Where-Object { $got -notcontains $_ }); $dup = @($got | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
      $pawns = @($st.players | Where-Object { $_.pawn }).Count
      Say "names on $c ($($st.role)): $($got -join ', ') | pawns $pawns | missing [$($missing -join ',')] duplicates [$($dup -join ',')]"
      if ($missing.Count -or $dup.Count) { $allOk = $false }
    }
    Say "NAMES: $(if ($allOk) { 'PASS every copy knows all names once' } else { 'FAIL' })"
    $py = Join-Path $PSScriptRoot "shotgrid.py"
    & python -I $py $Out | ForEach-Object { Say "grid: $_" }
  }
  $n = Select-String -Path (Log "A") -Pattern "stat: npc=(\d+)" | Select-Object -Last 1
  $npc = if ($n) { [int]$n.Matches[0].Groups[1].Value } else { 0 }
  Say "host sees $npc player controller(s)"
  if ($npc -ge $Copies.Count) { Say "RESULT: PASS $($Copies.Count) players through the Escalation menus" } else { Say "RESULT: FAIL host sees $npc of $($Copies.Count) players" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
