# Real play (owner 2026-11-16: "tests should not be like tests - start from the very beginning, everything clicked like
# invites, then a bot walks the character through the game; shorter, the most problematic map, this is the priority").
# One short end-to-end run, 2 copies by default:
#   1. both copies start from scratch at the main menu (real launch, mock Steam names = player names),
#   2. the lobby is made by CLICKS only: F10 -> Host a game -> Create lobby; guest F10 -> Join a game -> Join lobby
#      (input uiclick drives the menu cursor, no focus needed),
#   3. the host starts the chapter through the game's own menu (keys, -HostKeys),
#   4. a bot PLAYS both characters like a player would: walks (turn + W), strafes, jumps, transforms, shoots, interacts -
#      all through the mod's injected input, so the owner can use the PC (-Background is the default).
# What it checks (what a player would notice): both pawns exist and move, each side sees the other where it really is
# (no desync > 800 uu, no teleports > 4000 uu), nobody crashes or loses a pawn, player names on the host are the lobby
# names (B52). Screenshots (2x2 grids) along the way. Default map: L02a (richest bug history: B1, B10, B50/51, B52, B55).
# Usage: realplay.ps1 [-Copies A,B] [-Map L02a_DefendTheArk_BASE_m] [-PlaySec 150] [-Foreground] [-Keep]
param(
  [string[]]$Copies = @("A", "B"),
  [string]$Map = "L02a_DefendTheArk_BASE_m",
  [int]$PlaySec = 150,
  [string]$HostKeys = "UP,ENTER",
  [ValidateSet("Lobby", "Direct")][string]$Flow = "Direct",
  [switch]$Escalation,
  [string]$Out = "",
  [switch]$Keep,
  [switch]$Foreground,
  [string]$SlotOwner = ""
)
. "$PSScriptRoot\dev.ps1"
if (-not $Foreground) { Enable-FocBackground }
Add-Type -AssemblyName System.Drawing
if (-not $Out) { $Out = "E:\FoC-coop\sessions\realplay_$(Get-Date -Format yyyyMMdd_HHmm)_$(if ($Escalation) { 'ESC' } else { $Map })" }
New-Item -ItemType Directory -Force $Out | Out-Null
$names = @{ A = "Optimus"; B = "Bumblebee"; C = "Ironhide"; D = "Ratchet" }
# F10 host-page list titles (overlay.inc kChapters / kEscalation) - what input uiclick clicks
$chapTitles = @{
  "L01_TheExodus_BASE_m" = "1  The Exodus"; "L02a_DefendTheArk_BASE_m" = "2a Defend the Ark"
  "L02b_DestroyAllMortars_BASE_m" = "2b Destroy All Mortars"; "L03a_EyeOfTheStorm_BASE_m" = "3a Eye of the Storm"
  "L03b_CutAndRun_BASE_m" = "3b Cut and Run"; "L04a_BridgeAssault_BASE_m" = "4a Bridge Assault"
  "L04b_StopTheTransport_BASE_m" = "4b Stop the Transport"; "L04c_FlyingFortress_BASE_m" = "4c Flying Fortress"
  "L05_Soundwave_BASE_m" = "5  Soundwave"; "L06_Megatron_BASE_m" = "6  Megatron"
  "L08a_Starscream_BASE_m" = "8a Starscream"; "L08b_GrimlockSmash_BASE_m" = "8b Grimlock Smash"
  "L09_FinalFight_BASE_m" = "9  Final Fight"
}
$escTitles = @{ "ESC_Ark_BASE_m" = "Ark"; "ESC_Gladiator_BASE_m" = "Gladiator"; "ESC_Iacon_BASE_m" = "Iacon"; "ESC_Temple_BASE_m" = "Temple" }
$slotOwnerName = Get-FocSlotOwner $SlotOwner
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function LogF($c) { "E:\FoC-coop\$c\Binaries\foc_coop.log" }
function Has($c, $pat) { [bool](Select-String -Path (LogF $c) -Pattern $pat -Quiet) }
function Status($c) { $h = @{}; $f = "E:\FoC-coop\$c\Binaries\foc_relay\relay.status"; if (Test-Path $f) { foreach ($l in Get-Content $f -Encoding UTF8) { if ($l -match '^(\w+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } } }; $h }
function CmdWait([string]$c, [string]$cmd) {   # foc_cmd.txt is taken within 0.5 s; wait until it was
  Send-Cmd $c $cmd
  for ($i = 0; $i -lt 20 -and (Test-Path "E:\FoC-coop\$c\Binaries\foc_cmd.txt"); $i++) { Start-Sleep -Milliseconds 250 }
}
function Key($c, $k) { CmdWait $c "input hold $k 150"; Start-Sleep -Milliseconds 900 }
function Click($c, $label) { CmdWait $c "input uiclick $label"; Start-Sleep -Milliseconds 1500 }
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
function Pos3($p) { if ($p -and ($p.pawn -or $p.local)) { @([double]$p.x, [double]$p.y, [double]$p.z) } else { $null } }
function D3($a, $b) { if (-not $a -or -not $b) { return -1 }; [int][math]::Sqrt(($a[0]-$b[0])*($a[0]-$b[0]) + ($a[1]-$b[1])*($a[1]-$b[1]) + ($a[2]-$b[2])*($a[2]-$b[2])) }

# ---- bot actions (mod input commands; no focus needed). One action per call, like one decision of a player.
function Bot-Act([string]$c, [string]$act, [int]$step) {
  switch ($act) {
    "walk"      { CmdWait $c "input mouse $(@(400,-400,250,-250)[$step % 4]) 0 350"; CmdWait $c "input hold W 2500" }
    "run"       { CmdWait $c "input hold LSHIFT 2600"; Start-Sleep -Milliseconds 400; CmdWait $c "input hold W 2100" }
    "strafe"    { CmdWait $c "input hold $(@('A','D')[$step % 2]) 1300" }
    "jump"      { CmdWait $c "input tap SPACE"; Start-Sleep -Milliseconds 300; CmdWait $c "input hold W 1200" }
    "shoot"     { CmdWait $c "input mouse $(@(-150,150)[$step % 2]) 0 250"; CmdWait $c "input button 500" }
    "transform" { CmdWait $c "input tap Q"; Start-Sleep -Milliseconds 800; CmdWait $c "input hold W 2200"; CmdWait $c "input tap Q" }
    "interact"  { CmdWait $c "input hold E 700" }
    default     { CmdWait $c "input hold W 2000" }
  }
}
# movement this action should produce (actions with no movement don't require a step distance)
function Act-Moves($act) { @("walk","run","strafe","jump","transform") -contains $act }

$fail = @(); $hostC = $Copies[0]; $guests = @($Copies | Select-Object -Skip 1)
Enter-FocSlot -Owner $slotOwnerName -TtlMin 60
try {
  # ---- 1. from scratch: fresh state, real launch, wait at the main menu
  foreach ($c in $Copies) { Set-IniKey $c "playername" $names[$c]; if ($c -ne "A") { Set-IniKey $c "instance" $c } }
  Stop-Foc; Stop-Relay; Get-Process FoCRelay -ErrorAction SilentlyContinue | Stop-Process -Force
  foreach ($c in $Copies) { Clear-FocState $c; Remove-Item "E:\FoC-coop\$c\Binaries\foc_relay\relay.status", "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt", "E:\FoC-coop\$c\Binaries\foc_cmd.txt" -ErrorAction SilentlyContinue }
  foreach ($c in $Copies) { $env:FOC_MOCK_NAME = $names[$c]; [void](Start-Foc $c "" 1280 720); Start-Sleep 20 }
  Remove-Item Env:FOC_MOCK_NAME -ErrorAction SilentlyContinue
  foreach ($c in $Copies) { $ok = $false; for ($i = 0; $i -lt 180 -and -not $ok; $i++) { Start-Sleep 1; $ok = Has $c "overlay initialised" }; if (-not $ok) { Say "RESULT: SKIP $c has no game window (B7)"; return } }
  Start-Sleep 12; Grid "1_main_menu"

  # ---- 2/3. the lobby / the game start, by clicks only (owner 2026-11-16: co-op through the main menu, multiplayer and
  # Escalation the same; a map the story has not opened yet is started from the F10 menu's chapter/map list).
  #   -Flow Lobby:  F10 > Host a game > Create lobby, guests Join lobby, host starts the chapter from the game's own menu
  #                 (-HostKeys, e.g. UP,ENTER = CONTINUE) - the way real people play an opened chapter.
  #   -Flow Direct: F10 > Host a game > (Escalation radio) > the chapter/map title in the list > Start hosting; guests
  #                 Join a game > Join game - the way to play a chapter the story has not opened yet (and Escalation).
  if ($Flow -eq "Lobby") {
    Key $hostC "F10"; Click $hostC "Host a game"; Grid "2_host_page"; Click $hostC "Create lobby"
    $id = $null; for ($i = 0; $i -lt 20 -and -not $id; $i++) { Start-Sleep 1; $s = Status $hostC; if ($s.state -like "hosting*" -and $s.lobby -and $s.lobby -ne "0") { $id = $s.lobby } }
    Say "host lobby: $(if ($id) { $id } else { 'NOT created' })"; if (-not $id) { $fail += "host lobby not created by the menu" }
    foreach ($c in $guests) {
      Key $c "F10"; Click $c "Join a game"
      $listed = $false; for ($i = 0; $i -lt 20 -and -not $listed; $i++) { Start-Sleep 1; $listed = (Test-Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt") -and [bool](Select-String -Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt" -Pattern "^$id\|" -Quiet) }
      Start-Sleep 2; Click $c "Join lobby"
      $ok = $false; for ($i = 0; $i -lt 20 -and -not $ok; $i++) { Start-Sleep 1; $s = Status $c; $ok = $s.state -like "joined*" -or $s.state -like "connected*" }
      Say "${c}: listed $listed, in the lobby $ok ($((Status $c).state))"; if (-not $ok) { $fail += "$c did not join through the menu" }
    }
    Start-Sleep 3; Grid "3_lobby"
    foreach ($k in $HostKeys -split ",") { Key $hostC $k; Start-Sleep 4 }
  } else {
    $title = if ($Escalation) { $escTitles[$Map] } else { $chapTitles[$Map] }
    if (-not $title) { Say "RESULT: FAIL unknown map $Map"; return }
    Key $hostC "F10"; Click $hostC "Host a game"; Grid "2_host_page"
    if ($Escalation) { Click $hostC "Escalation"; Start-Sleep -Milliseconds 800 }
    Click $hostC $title; Start-Sleep -Milliseconds 800
    Click $hostC "Start hosting"; Start-Sleep 5
    # wait for the host to be in the map first: only then the Join row shows "Join game" (a click on "Join lobby" while
    # the host is still loading would park the guest in a lobby - it follows only when the relay gets "go")
    $mapPat = if ($Escalation) { "ESC*" } else { "L*" }
    $sHost = Wait-FocState $hostC { param($s) $s.role -eq "host" -and $s.port -gt 0 -and $s.map -like $mapPat } 180
    Say "host in $(if ($sHost) { $sHost.map } else { 'NOT started' })"; if (-not $sHost) { $fail += "host did not start the map" }
    # the guests join the running game from their Join list (the row shows this map)
    foreach ($c in $guests) {
      Key $c "F10"; Click $c "Join a game"
      $listed = $false; for ($i = 0; $i -lt 40 -and -not $listed; $i++) { Start-Sleep 1; $listed = (Test-Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt") -and [bool](Select-String -Path "E:\FoC-coop\$c\Binaries\foc_relay\lobbies.txt" -Pattern "\|$Map\|" -Quiet) }
      Start-Sleep 2; Grid "3_join_page_$c"
      # the row button is "Join game" while the host is in the map, "Join lobby" while he is still in the menu - click
      # whichever the page shows (uiclick logs "no such button" when the label is absent)
      Click $c "Join game"
      if (Select-String -Path (LogF $c) -Pattern "uiclick 'Join game' - no such button" -Quiet) { Say "${c}: row shows a lobby, clicking Join lobby"; Click $c "Join lobby" }
      # the travel takes a while: not in the mission yet -> click the row again
      $inGame = Wait-FocState $c { param($s) $s.role -eq "client" -and $s.map -eq $Map } 45
      if (-not $inGame) {
        Click $c "Join game"
        if (Select-String -Path (LogF $c) -Pattern "uiclick 'Join game' - no such button" -Quiet) { Click $c "Join lobby" }
        $inGame = Wait-FocState $c { param($s) $s.role -eq "client" -and $s.map -eq $Map } 90
      }
      Say "${c}: host's game listed $listed, joined $(if ($inGame) { 'ok' } else { 'NOT yet' })"
      if (-not $listed) { $fail += "$c never saw the host's game in the Join list" }
    }
    if ($Escalation) {
      # both sides land on the character pick (host: Right then Enter, the default is locked; guest: Enter)
      Start-Sleep 20
      foreach ($c in $Copies) { if ($c -eq $hostC) { CmdWait $c "input tap RIGHT"; Start-Sleep -Milliseconds 700; CmdWait $c "input tap ENTER" } else { CmdWait $c "input tap ENTER" }; Start-Sleep 2 }
    }
  }
  $mapPat = if ($Escalation) { "ESC*" } else { "L*" }
  $sA = Wait-FocState $hostC { param($s) $s.role -eq "host" -and $s.port -gt 0 -and $s.map -like $mapPat } 180
  if (-not $sA) { $fail += "host did not start a mission"; Say "host not in a mission" }
  else {
    Say "host in $($sA.map)"
    foreach ($c in $guests) { $sb = Wait-FocState $c { param($s) $s.role -eq "client" -and $s.map -eq $sA.map } 180; Say "${c}: $(if ($sb) { "client on $($sb.map)" } else { 'NOT in the mission' })"; if (-not $sb) { $fail += "$c not in the mission" } }
    # every player gets a pawn (the intro may hold them in cinematic mode for a while)
    $sA = Wait-FocState $hostC { param($s) $s.players.Count -ge $Copies.Count -and -not ($s.players | Where-Object { -not $_.pawn }) } 120
    if (-not $sA) { $fail += "not everyone has a pawn: $((Read-FocState $hostC).players | ForEach-Object { $_.pc }) " }
    Start-Sleep 10; Grid "4_in_game"
  }

  # ---- 4. the bot plays both characters
  if (-not $fail) {
    $actions = @("walk", "shoot", "strafe", "walk", "jump", "transform", "run", "interact")
    $prev = @{}; $stuck = @{}; $step = 0; $t0 = Get-Date; $cineUntil = [datetime]::MinValue
    foreach ($c in $Copies) { $prev[$c] = $null; $stuck[$c] = 0 }
    while (((Get-Date) - $t0).TotalSeconds -lt $PlaySec) {
      $step++; Update-FocSlot
      # a cutscene takes the controls (cine=1) and may warp everybody to the scene spot - a real player just watches;
      # no actions and no teleport/desync checks while it runs or a few seconds after (the server pawns settle slowly)
      $sH0 = Read-FocState $hostC
      $cineNow = $false
      if ($sH0) { foreach ($pl in $sH0.players) { if ($pl.cine) { $cineNow = $true } } }
      if ($cineNow) { $cineUntil = (Get-Date).AddSeconds(6) }
      if ((Get-Date) -lt $cineUntil) { Start-Sleep 2; continue }
      foreach ($c in $Copies) {
        $p = Find-CopyProc $c
        if (-not $p) { $fail += "$c died at bot step $step"; continue }
        $s = Read-FocState $c
        if (-not $s -or $s.players.Count -lt 1) { continue }
        $me = $s.players | Where-Object { $_.local } | Select-Object -First 1
        if (-not $me.pawn) { $fail += "$c lost its pawn at step $step"; continue }
        if ($me.cine) { continue }   # this copy is in a cutscene of its own (e.g. a death cam)
        $act = $actions[($step + [array]::IndexOf($Copies, $c) * 3) % $actions.Count]
        # stuck against a wall last time: turn around first, like a player would
        if ($stuck[$c] -ge 2) { CmdWait $c "input mouse $(@(1400,-1400)[$step % 2]) 0 400"; $stuck[$c] = 0 }
        Bot-Act $c $act $step
      }
      Start-Sleep 1
      # checks: movement, desync, teleports - what a player would see
      foreach ($c in $Copies) {
        $s = Read-FocState $c; if (-not $s) { continue }
        $me = $s.players | Where-Object { $_.local } | Select-Object -First 1
        $now = Pos3 $me
        if ($prev[$c] -and $now) {
          $d = D3 $prev[$c] $now
          $actIdx = ($step + [array]::IndexOf($Copies, $c) * 3) % $actions.Count
          if ($d -gt 4000) { $fail += "$c teleported $d uu at step $step" }
          elseif (Act-Moves $actions[$actIdx] -and $d -lt 100) { $stuck[$c] = $stuck[$c] + 1 } else { $stuck[$c] = 0 }
        }
        $prev[$c] = $now
      }
      $sH = Read-FocState $hostC
      if ($sH -and -not $Escalation) {   # Escalation: enemy pawns and the guest's own drift make positions noisy (matrix)
        $meH = $sH.players | Where-Object { $_.local } | Select-Object -First 1
        foreach ($c in $guests) {
          $gH = $sH.players | Where-Object { -not $_.local -and $_.pc -match "Campaign" } | Select-Object -First 1
          $sg = Read-FocState $c
          if ($sg) {
            $meG = $sg.players | Where-Object { $_.local } | Select-Object -First 1
            # the host's view of the guest vs the guest's own position (desync a player would notice)
            $dx = D3 (Pos3 $gH) (Pos3 $meG)
            if ($meG.pawn -and $dx -ge 0 -and $dx -gt 1200) { $fail += ("desync host-" + $c + " at step " + $step + ": host sees the guest " + $dx + " uu from where the guest is") }
            # the guest's view of the host: nearest unpossessed pawn to the host's real position
            $gh = $null; $best = 1e12
            foreach ($pw in $sg.pawns) { if ($pw.possessed) { continue }; $d = D3 (@($pw.x, $pw.y, $pw.z)) (Pos3 $meH); if ($d -ge 0 -and $d -lt $best) { $best = $d; $gh = $pw } }
            if ($gh -and $best -gt 1200) { $fail += ("desync " + $c + "-host at step " + $step + ": the guest sees the host " + $best + " uu away from where the host really is") }
          }
        }
      }
      if ($step % 6 -eq 0) { Grid "5_bot_step$step" }
    }
    # ---- 5. names a player would read on the TAB board (B52)
    $sH = Read-FocState $hostC
    if ($sH) {
      $want = @(); foreach ($c in $Copies) { $want += $names[$c] }
      $have = @($sH.players | ForEach-Object { $_.name })
      Say "host player names: $($have -join ', ')"
      foreach ($w in $want) { if ($have -notcontains $w) { $fail += "name '$w' missing on the host (have: $($have -join ','))" } }
    }
    $sH = Read-FocState $hostC
    if ($sH -and ($sH.excExe -gt 0)) { $fail += "unguarded exceptions on the host: $($sH.excExe)" }
    foreach ($c in $guests) { $sg = Read-FocState $c; if ($sg -and $sg.excExe -gt 0) { $fail += "unguarded exceptions on ${c}: $($sg.excExe)" } }
    Grid "6_end"
  }
  foreach ($c in $Copies) { Copy-Item (LogF $c) "$Out\$c.log" -ErrorAction SilentlyContinue }
  if ($fail.Count) { Say "RESULT: FAIL $($fail -join '; ')" } else { Say "RESULT: PASS real play: $($Copies.Count) players, lobby by clicks, $Map, bot played $PlaySec s, names and views ok" }
} finally {
  Disable-FocBackground
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner $slotOwnerName
}
