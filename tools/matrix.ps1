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
  [switch]$Keep             # leave both games running after the last map
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
    Get-ChildItem "$bin\foc_crash_*.dmp" -ErrorAction SilentlyContinue | ForEach-Object { Move-Item $_.FullName "$Out\$map`_$c`_$($_.Name)" -Force }
  }
}
function PawnSummary($s) { ($s.players | ForEach-Object { "$(if ($_.local) {'host'} else {'guest'}):$(if ($_.pawn) {'pawn'} else {'nopawn'})/spawn=$($_.spawn)" }) -join ' ' }

foreach ($map in $Maps) {
  if ([W]::GetForegroundWindow() -eq [IntPtr]::Zero) {
    # remote desktop session not displayed: the game cannot create its D3D device and crashes at start (BUGS B7)
    $line = "RESULT: SKIP $map no display (remote desktop window minimized or session locked)"
    Add-Content $report "$(Get-Date -Format s) $line"; $line; continue
  }
  Stop-Foc; Start-Sleep 3
  Clear-FocState A; Clear-FocState B
  Get-ChildItem "E:\FoC-coop\A\Binaries\foc_crash_*.dmp", "E:\FoC-coop\B\Binaries\foc_crash_*.dmp" -ErrorAction SilentlyContinue | Remove-Item
  $isStory = $map -like "L*"
  $game = if ($isStory) { "TnCampaignGame" } else { "TnSurvivalGame" }
  $reason = $null; $detail = ""
  $pidA = Start-Foc "A" "$map`?Game=TransContent.$game`?listen`?MaxPlayers=$Players" 960 540
  # 1. the host is in the map, with the expected game class (checks the mode before anything else)
  $sA = Wait-FocState A { param($s) $s.map -eq $map -and $s.role -eq "host" -and $s.port -gt 0 } 180
  if (-not $sA) { $s = Read-FocState A; $reason = "host not in map (alive=$(Alive $pidA) map=$($s.map) role=$($s.role) port=$($s.port))" }
  elseif ($sA.game -ne $game) { $reason = "host game class is $($sA.game), expected $game" }
  if (-not $reason) {
    $hA = Wait-FocWindow $pidA 30
    # 2. the guest joins the same map
    $pidB = Start-Foc "B" "127.0.0.1:$($sA.port)" 960 540
    $sB = Wait-FocState B { param($s) $s.map -eq $map -and $s.role -eq "client" } 150
    if (-not $sB) { $s = Read-FocState B; $reason = "guest not in map (alive=$(Alive $pidB) map=$($s.map) role=$($s.role))" }
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
  Save-Artifacts $map
  $line = if ($reason) { $fail++; "RESULT: FAIL $map $reason" } else { $pass++; "RESULT: PASS $map$detail" }
  Add-Content $report "$(Get-Date -Format s) $line"
  $line
  if (-not $Keep) { Stop-Foc }
}
"SUMMARY: pass=$pass fail=$fail (logs in $Out)"
