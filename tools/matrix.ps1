# Map matrix: for each map start a host (copy A) and a guest (copy B) and check the game state that the DLL writes
# (foc_state.json in each copy's Binaries, see gametick.inc) instead of judging screenshots.
# One line per map: "RESULT: PASS <map> ..." or "RESULT: FAIL <map> <reason>", then "SUMMARY: pass=N fail=M".
# Logs and crash dumps of every run are copied to $Out.
param(
  [string[]]$Maps = @("ESC_Ark_BASE_m"),
  [string]$Out = "$env:TEMP\foc_matrix",
  [int]$Players = 2,
  [int]$PawnTimeout = 90,   # seconds after the guest joined until every player must have a pawn
  [int]$AliveSec = 60,      # then both games must stay alive and keep their pawns this long
  [switch]$Move,            # also hold W in each window (real input, needs focus) and require the pawns to move
  [switch]$Look,            # in-game screenshots of host and guest before/after both walk forward (injected input, no focus)
  [switch]$Keep,            # leave both games running after the last map
  [switch]$Steam            # through FoCRelay (Steam Spacewar; test copies use the mock steam_api.dll)
)
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
$report = Join-Path $Out "report.txt"
$pass = 0; $fail = 0

function Alive($procId) { [bool](Get-Process -Id $procId -ErrorAction SilentlyContinue) }
function Save-Artifacts($map) {
  foreach ($c in "A", "B") {
    $bin = "E:\FoC-coop\$c\Binaries"
    Copy-Item "$bin\foc_coop.log" "$Out\$map`_$c.log" -ErrorAction SilentlyContinue
    Copy-Item "$bin\foc_state.json" "$Out\$map`_$c.state.json" -ErrorAction SilentlyContinue
    Copy-Item "$bin\foc_relay\relay.status" "$Out\$map`_$c.relay.status" -ErrorAction SilentlyContinue
    Get-ChildItem "$bin\foc_crash_*.dmp" -ErrorAction SilentlyContinue | ForEach-Object { Move-Item $_.FullName "$Out\$map`_$c`_$($_.Name)" -Force }
  }
}
function Send-CmdWait([string]$Copy, [string]$Cmd) {   # foc_cmd.txt is read every 0.5 s; wait until it was taken
  Send-Cmd $Copy $Cmd
  for ($i = 0; $i -lt 20 -and (Test-Path "E:\FoC-coop\$Copy\Binaries\foc_cmd.txt"); $i++) { Start-Sleep -Milliseconds 250 }
}
function Shot-Both($map, $tag) {
  Add-Type -AssemblyName System.Drawing
  foreach ($c in "A", "B") { Send-CmdWait $c "shot $Out\$map`_$c`_$tag.bmp" }
  Start-Sleep 2
  foreach ($c in "A", "B") {
    $bmp = "$Out\$map`_$c`_$tag.bmp"
    if (Test-Path $bmp) { $im = [System.Drawing.Image]::FromFile($bmp); $im.Save(($bmp -replace '\.bmp$', '.png'), [System.Drawing.Imaging.ImageFormat]::Png); $im.Dispose(); Remove-Item $bmp }
  }
}
function LocalPos($c) { $s = Read-FocState $c; $p = $s.players | Where-Object { $_.local } | Select-Object -First 1; if ($p -and $p.pawn) { @($p.x, $p.y) } else { $null } }
function Dist($a, $b) { if (-not $a -or -not $b) { return -1 }; [int][math]::Sqrt([math]::Pow($a[0] - $b[0], 2) + [math]::Pow($a[1] - $b[1], 2)) }
# Look step: screenshots of host and guest, only the guest walks forward (real W key), screenshots again.
# The host did not move, so its picture must stay about the same; if it changes as much as the guest's, the host
# camera follows the guest's pawn (BUGS B10). Returns "" or a failure reason, and appends details to $script:detail.
function ImgDiff($p1, $p2) {   # mean absolute difference of 32x18 grayscale thumbnails, 0..255
  Add-Type -AssemblyName System.Drawing
  $t = foreach ($p in $p1, $p2) { $im = [Drawing.Image]::FromFile($p); $b = New-Object Drawing.Bitmap 32, 18; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($im, 0, 0, 32, 18); $g.Dispose(); $im.Dispose(); , $b }
  $sum = 0
  for ($y = 0; $y -lt 18; $y++) { for ($x = 0; $x -lt 32; $x++) { $c1 = $t[0].GetPixel($x, $y); $c2 = $t[1].GetPixel($x, $y); $sum += [math]::Abs(($c1.R + $c1.G + $c1.B) - ($c2.R + $c2.G + $c2.B)) / 3 } }
  $t[0].Dispose(); $t[1].Dispose()
  [int]($sum / (32 * 18))
}
function Look-Both($map, $pidA, $pidB) {
  Shot-Both $map "1before"
  $a0 = LocalPos A; $b0 = LocalPos B
  $hB = Wait-FocWindow $pidB 5; HoldKey $hB 0x11 3000; Start-Sleep 2
  $a1 = LocalPos A; $b1 = LocalPos B
  Shot-Both $map "2after"
  $hm = Dist $a0 $a1; $gm = Dist $b0 $b1
  $da = ImgDiff "$Out\$map`_A_1before.png" "$Out\$map`_A_2after.png"
  $db = ImgDiff "$Out\$map`_B_1before.png" "$Out\$map`_B_2after.png"
  $script:detail += " hostMoved=$hm guestMoved=$gm hostImgDiff=$da guestImgDiff=$db"
  if ($map -notlike "L*") { return "" }   # Escalation: moving enemies and effects, the state file has no stable guest position; screenshots only
  if ($gm -lt 200) { return "guest did not move ($gm uu)" }
  if ($hm -lt 50 -and $da -gt [math]::Max(12, $db / 2)) { return "host picture changed ($da) although only the guest moved: host camera follows the guest (B10)" }
  return ""
}
function PawnSummary($s) { ($s.players | ForEach-Object { "$(if ($_.local) {'host'} else {'guest'}):$(if ($_.pawn) {'pawn'} else {'nopawn'})/spawn=$($_.spawn)" }) -join ' ' }

foreach ($map in $Maps) {
  if ([W]::GetForegroundWindow() -eq [IntPtr]::Zero) {
    # remote desktop session not displayed: the game cannot create its D3D device and crashes at start (BUGS B7)
    $line = "RESULT: SKIP $map no display (remote desktop window minimized or session locked)"
    Add-Content $report "$(Get-Date -Format s) $line"; $line; continue
  }
  Stop-Foc; Stop-Relay; Start-Sleep 3
  Clear-FocState A; Clear-FocState B
  if ($Steam) { Remove-Item -Recurse -Force "$env:TEMP\foc_mock_steam", "E:\FoC-coop\A\Binaries\foc_relay\relay.status", "E:\FoC-coop\B\Binaries\foc_relay\relay.status" -ErrorAction SilentlyContinue }
  Get-ChildItem "E:\FoC-coop\A\Binaries\foc_crash_*.dmp", "E:\FoC-coop\B\Binaries\foc_crash_*.dmp" -ErrorAction SilentlyContinue | Remove-Item
  $isStory = $map -like "L*"
  $game = if ($isStory) { "TnCampaignGame" } else { "TnSurvivalGame" }
  $reason = $null; $detail = ""; $pidB = $null
  $extraA = @(); if ($Steam) { $extraA = @("-steamrelay") }
  $pidA = Start-Foc "A" "$map`?Game=TransContent.$game`?listen`?MaxPlayers=$Players" 960 540 -Extra $extraA
  # 1. the host is in the map, with the expected game class (checks the mode before anything else)
  $sA = Wait-FocState A { param($s) $s.map -eq $map -and $s.role -eq "host" -and $s.port -gt 0 } 180
  if (-not $sA) { $s = Read-FocState A; $reason = "host not in map (alive=$(Alive $pidA) map=$($s.map) role=$($s.role) port=$($s.port))" }
  elseif ($sA.game -ne $game) { $reason = "host game class is $($sA.game), expected $game" }
  if (-not $reason -and $Steam) {
    # Steam mode: the host's DLL starts FoCRelay host; its lobby must open
    for ($i = 0; $i -lt 60 -and (Read-RelayStatus A)["state"] -notlike "hosting*"; $i++) { Start-Sleep 1 }
    $rs = Read-RelayStatus A
    if ($rs["state"] -notlike "hosting*") { $reason = "host relay not hosting (state: $($rs['state']))" } else { $detail += " lobby=$($rs['lobby'])" }
  }
  if (-not $reason) {
    $hA = Wait-FocWindow $pidA 30
    # 2. the guest joins the same map (Steam mode: from the main menu with the Join page's Steam button = `steamjoin`)
    if ($Steam) {
      $pidB = Start-Foc "B" "" 960 540
      $s0 = Wait-FocState B { param($s) $s.map -like "UI_FrontEnd*" } 120
      Start-Sleep 5; Send-Cmd B "steamjoin"
      $sB = Wait-FocState B { param($s) $s.map -eq $map -and $s.role -eq "client" } 180
      $p = Find-CopyProc B; if ($p) { $pidB = $p.Id }
    } else {
      $pidB = Start-Foc "B" "127.0.0.1:$($sA.port)" 960 540
      $sB = Wait-FocState B { param($s) $s.map -eq $map -and $s.role -eq "client" } 150
    }
    if (-not $sB) { $s = Read-FocState B; $reason = "guest not in map (alive=$(Alive $pidB) map=$($s.map) role=$($s.role) relay=$((Read-RelayStatus B)['state']))" }
  }
  if (-not $reason) {
    $sA = Wait-FocState A { param($s) $s.players.Count -ge 2 } 60
    if (-not $sA) { $reason = "host does not see the guest (players=$((Read-FocState A).players.Count))" }
  }
  $pickChars = {
    # Escalation: both pick a character (host: Right then Enter, the default one is locked, see BUGS B3).
    # The guest's character screen needs time after joining: an early Enter there drops it back to the main menu.
    $hB = Wait-FocWindow $pidB 30
    FocusWin $hB; [W]::Key(0x0D); Start-Sleep 2
    FocusWin $hA; [W]::Key(0x27); Start-Sleep -Milliseconds 700; [W]::Key(0x0D)
  }
  if (-not $reason -and -not $isStory) { Start-Sleep 25; & $pickChars }
  if (-not $reason) {
    # 3. every player gets a pawn (Escalation: pick again once if nobody spawned after 30 s)
    $sA = Wait-FocState A { param($s) $s.players.Count -ge 2 -and -not ($s.players | Where-Object { -not $_.pawn }) } $(if ($isStory) { $PawnTimeout } else { 30 })
    if (-not $sA -and -not $isStory -and (Read-FocState A).players.Count -ge 2) {
      & $pickChars
      $sA = Wait-FocState A { param($s) $s.players.Count -ge 2 -and -not ($s.players | Where-Object { -not $_.pawn }) } $PawnTimeout
    }
    if (-not $sA) { $s = Read-FocState A; $reason = "not every player has a pawn after $PawnTimeout s: $(PawnSummary $s)" }
  }
  if (-not $reason -and $Move) {
    # 4. both pawns move (real key presses; DirectInput needs scan codes)
    $before = Read-FocState A
    $hA = Wait-FocWindow $pidA 5; $hB = Wait-FocWindow $pidB 5
    HoldKey $hA 0x11 1500; HoldKey $hB 0x11 1500; Start-Sleep 2
    $after = Read-FocState A
    foreach ($i in 0..($after.players.Count - 1)) {
      $p0 = $before.players | Where-Object { $_.pc -eq $after.players[$i].pc }
      $d = [math]::Sqrt([math]::Pow($after.players[$i].x - $p0.x, 2) + [math]::Pow($after.players[$i].y - $p0.y, 2))
      $detail += " moved$i=$([int]$d)"
      if ($d -lt 50) { $reason = "pawn of $($after.players[$i].pc) did not move ($([int]$d) uu)" }
    }
  }
  if (-not $reason) {
    # 5. both stay alive, keep updating their state and keep their pawns
    $t0 = (Read-FocState A).t
    for ($i = 0; $i -lt $AliveSec -and -not $reason; $i += 5) {
      Start-Sleep 5
      if (-not (Alive $pidA)) { $reason = "host died after $i s in game" }
      elseif (-not (Alive $pidB)) { $reason = "guest died after $i s in game" }
    }
    if (-not $reason) {
      $s = Read-FocState A
      if ($s.t -le $t0) { $reason = "host state stopped updating (game thread stuck?)" }
      elseif ($s.players | Where-Object { -not $_.pawn }) { $reason = "a pawn was lost: $(PawnSummary $s)" }
      elseif ($s.excExe -gt 0 -or (Read-FocState B).excExe -gt 0) { $reason = "unguarded exceptions in TFOC.exe (host $($s.excExe), guest $((Read-FocState B).excExe))" }
      else { $detail += " $(PawnSummary $s) guards=$($s.guards -join ',') tickMsMax=$($s.tickMsMax)" }
    }
  }
  if ($Look -and -not $reason -and (Alive $pidA) -and $pidB -and (Alive $pidB)) { $script:detail = $detail; $r = Look-Both $map $pidA $pidB; $detail = $script:detail; if ($r) { $reason = $r + " |" + $detail } }
  if ($Steam -and -not $reason) {   # the game traffic really went through the relays
    $ra = Read-RelayStatus A; $rb = Read-RelayStatus B
    $detail += " relay host peers=$($ra['peers']) tx=$($ra['tx']) rx=$($ra['rx']) | guest state='$($rb['state'])' tx=$($rb['tx'])"
    if ([int64]$ra['rx'] -le 0 -or [int64]$rb['tx'] -le 0) { $reason = "no game traffic through the Steam relay |$detail" }
  }
  Save-Artifacts $map
  $line = if ($reason) { $fail++; "RESULT: FAIL $map $reason" } else { $pass++; "RESULT: PASS $map$detail" }
  Add-Content $report "$(Get-Date -Format s) $line"
  $line
  if (-not $Keep) { Stop-Foc }
}
"SUMMARY: pass=$pass fail=$fail (logs in $Out)"
