# Owner 2026-10-24: story co-op with up to 5 players through the F10 lobby (test with 4 copies, one map): the host is
# the server (enemies, damage shared), scenes play for everybody, and after a scene everybody is moved to the player who
# triggered it (scene.inc). Steps: A creates a lobby, B/C/D join, A starts the map through the hooked menu Exec, all 4
# must be on the map; the intro cutscene must not move anybody (level script, no activator); D walks away (W held),
# then `scenegather <D>` on the host must bring A, B, C within 1200 uu of D. Screenshots of all copies (2x2 grids).
param([string]$Map = "L02a_DefendTheArk_BASE_m", [string]$Out = "E:\FoC-coop\sessions\scene4", [switch]$Keep, [string]$CineOp = "SeqAct_ToggleCinematicMode_1740")
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$copies = @("A", "B", "C", "D"); $names = @{ A = "Optimus"; B = "Bumblebee"; C = "Ironhide"; D = "Ratchet" }
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Log($c) { "E:\FoC-coop\$c\Binaries\foc_coop.log" }
function Has($c, $pat) { [bool](Select-String -Path (Log $c) -Pattern $pat -Quiet) }
function Status($c) { $h = @{}; $f = "E:\FoC-coop\$c\Binaries\foc_relay\relay.status"; if (Test-Path $f) { foreach ($l in Get-Content $f -Encoding UTF8) { if ($l -match '^(\w+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } } }; $h }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" }); if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
function Grid($tag) {
  foreach ($c in $copies) { Send-Cmd $c "shotui $Out\$tag`_$c.bmp" }; Start-Sleep 4
  $g = New-Object Drawing.Bitmap 1920, 1080; $gr = [Drawing.Graphics]::FromImage($g); $i = 0
  foreach ($c in $copies) { $f = "$Out\$tag`_$c.bmp"; if (Test-Path $f) { $im = [Drawing.Image]::FromFile($f); $gr.DrawImage($im, ($i % 2) * 960, [int][Math]::Floor($i / 2) * 540, 960, 540); $im.Dispose(); Remove-Item $f }; $i++ }
  $g.Save("$Out\$tag.png", [Drawing.Imaging.ImageFormat]::Png); $gr.Dispose(); $g.Dispose()
}
function GuestViews($target, $skip) {   # each guest's own pawn (its foc_state local player) vs the target from the host
  $bad = @(); $txt = @()
  foreach ($c in "B", "C", "D") {
    if ($names[$c] -eq $skip) { continue }
    $me = @((Read-FocState $c).players | ? { $_.local -eq 1 }) | Select-Object -First 1
    if (-not $me) { $txt += "$c ?"; continue }
    $dd = [int](Dist $me $target); $txt += "$($names[$c]) $dd"; if ($dd -gt 1500) { $bad += $names[$c] }
  }
  @{ bad = $bad; txt = ($txt -join ', ') }
}
function Dist($p, $q) { [Math]::Sqrt([Math]::Pow($p.x - $q.x, 2) + [Math]::Pow($p.y - $q.y, 2) + [Math]::Pow($p.z - $q.z, 2)) }
$fail = @()
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  foreach ($c in $copies) { Set-IniKey $c "playername" $names[$c]; if ($c -ne "A") { Set-IniKey $c "instance" $c } }
  Stop-Foc; Stop-Relay; Get-Process FoCRelay -ErrorAction SilentlyContinue | Stop-Process -Force
  foreach ($c in $copies) { Clear-FocState $c; Remove-Item "E:\FoC-coop\$c\Binaries\foc_relay\relay.status", "E:\FoC-coop\$c\Binaries\foc_cmd.txt" -ErrorAction SilentlyContinue }
  foreach ($c in $copies) { [void](Start-Foc $c "" 800 450); Start-Sleep 25 }
  foreach ($c in $copies) { $ok = $false; for ($i = 0; $i -lt 240 -and -not $ok; $i++) { Start-Sleep 1; $ok = Has $c "overlay initialised" }; if (-not $ok) { Say "RESULT: SKIP $c has no game window (B7)"; return } }
  Start-Sleep 15
  Send-Cmd A "lobby create 5"
  $id = $null; for ($i = 0; $i -lt 30 -and -not $id; $i++) { Start-Sleep 1; $s = Status A; if ($s.state -like "hosting*" -and $s.lobby -and $s.lobby -ne "0") { $id = $s.lobby } }
  if (-not $id) { Say "RESULT: FAIL host lobby not created"; return }
  foreach ($c in "B", "C", "D") {
    Send-Cmd $c "lobby join $id"
    $ok = $false; for ($i = 0; $i -lt 30 -and -not $ok; $i++) { Start-Sleep 1; $s = Status $c; $ok = $s.state -like "joined*" -or $s.state -like "connected*" }
    Say "$c in the lobby: $ok ($((Status $c).state))"; if (-not $ok) { $fail += "$c not in the lobby" }
  }
  Start-Sleep 3; Grid "1_lobby"
  Send-Cmd A "lobby menustart $Map"; Say "host starts $Map"
  $sa = Wait-FocState A { param($s) $s.map -eq $Map -and $s.role -eq "host" -and $s.port -gt 0 -and @($s.players).Count -ge 4 } 300
  if (-not $sa) { $n = @((Read-FocState A).players).Count; Say "RESULT: FAIL host has $n of 4 players on $Map"; foreach ($c in $copies) { Copy-Item (Log $c) "$Out\$c.log" -ErrorAction SilentlyContinue }; return }
  Say "host on $Map with $(@($sa.players).Count) players: $((@($sa.players) | % { $_.name }) -join ', ')"
  # intro / first cutscene: wait until nobody is in cinematic mode (max 150 s), screenshots meanwhile
  Start-Sleep 10; Grid "2_start"
  for ($i = 0; $i -lt 50; $i++) { $st = Read-FocState A; if (-not (@($st.players) | ? { $_.cine -eq 1 })) { break }; Start-Sleep 3 }
  Start-Sleep 5; Grid "3_after_intro"
  for ($i = 0; $i -lt 30; $i++) { $nm = @((Read-FocState A).players | % { $_.name }); if ($nm -contains "Ratchet" -and $nm -contains "Bumblebee") { break }; Start-Sleep 2 }   # names arrive late
  $st = Read-FocState A
  $moved = @(Select-String -Path (Log A) -Pattern "scene: .*moved" | % Line)
  Say "after the intro: scene lines on the host: $(@(Select-String -Path (Log A) -Pattern 'scene: ' | % { $_.Line.Substring($_.Line.IndexOf('scene:')) }) -join ' | ')"
  # D goes away (keys do not move a pawn far in a test window: the server moves it, `scenemove`), then everybody
  # gathers at D
  $pl = @($st.players); $di = -1; for ($k = 0; $k -lt $pl.Count; $k++) { if ($pl[$k].name -eq "Ratchet") { $di = $k } }
  if ($di -lt 0) { Say "RESULT: FAIL no player named Ratchet on the host"; return }
  $nearest = 0
  foreach ($o in @("2500 0 60", "-2500 0 60", "0 2500 60", "0 -2500 60", "1800 1800 60", "-1800 -1800 60", "3500 0 60", "-3500 0 60")) {
    Send-Cmd A "scenemove $di $o"; Start-Sleep 4
    $st = Read-FocState A; $pl = @($st.players); $d = $pl[$di]
    $nearest = ($pl | ? { $_.name -ne "Ratchet" } | % { Dist $_ $d } | Measure-Object -Minimum).Minimum
    if ($nearest -gt 1500) { break }
  }
  if ($nearest -le 1500) { $fail += "could not move Ratchet away (nearest $([int]$nearest))" }
  Start-Sleep 2; Grid "4a_apart"
  $st = Read-FocState A; $pl = @($st.players); $d = $pl[$di]
  Say ("before gather (distance to Ratchet): " + (($pl | % { "$($_.name) $([int](Dist $_ $d))" }) -join ', '))
  Send-Cmd A "scenegather $di"; Start-Sleep 4
  $st = Read-FocState A; $pl = @($st.players); $d = $pl[$di]
  $far = @($pl | ? { $_.name -ne "Ratchet" -and (Dist $_ $d) -gt 1200 })
  Say ("after gather (distance to Ratchet): " + (($pl | % { "$($_.name) $([int](Dist $_ $d))" }) -join ', '))
  if ($far.Count) { $fail += "not gathered at Ratchet: $(($far | % { $_.name }) -join ',')" }
  Start-Sleep 3; $gv = GuestViews $d "Ratchet"; Say "guests' own view (distance to Ratchet): $($gv.txt)"
  if ($gv.bad.Count) { $fail += "guest view not at Ratchet: $($gv.bad -join ',')" }
  Start-Sleep 3; Grid "4_gathered"
  Say "host scene log: $(@(Select-String -Path (Log A) -Pattern 'scene: |tp: scene' | Select-Object -Last 6 | % { $_.Line.Substring($_.Line.IndexOf(']') + 2) }) -join ' | ')"
  # 5: a whole scene: Bumblebee goes away and is the activator; the level's own cinematic-mode op on the host turns the
  # scene on (input 0) and off (input 1); 1.5 s after the end everybody must stand next to Bumblebee
  if ($CineOp) {
    $st = Read-FocState A; $pl = @($st.players); $bi = -1; for ($k = 0; $k -lt $pl.Count; $k++) { if ($pl[$k].name -eq "Bumblebee") { $bi = $k } }
    foreach ($o in @("2500 0 60", "-2500 0 60", "0 2500 60", "0 -2500 60", "3500 0 60", "-3500 0 60")) {
      Send-Cmd A "scenemove $bi $o"; Start-Sleep 4
      $st = Read-FocState A; $pl = @($st.players); $b = $pl[$bi]
      $nearest = ($pl | ? { $_.name -ne "Bumblebee" } | % { Dist $_ $b } | Measure-Object -Minimum).Minimum
      if ($nearest -gt 1500) { break }
    }
    Send-Cmd A "sceneact $bi"; Start-Sleep 2
    Send-Cmd A "kimpulse $CineOp 0"; Start-Sleep 5
    $cine = @((Read-FocState A).players | ? { $_.cine -eq 1 }).Count
    Grid "5a_scene"
    Send-Cmd A "kimpulse $CineOp 1"; Start-Sleep 6
    $st = Read-FocState A; $pl = @($st.players); $b = $pl[$bi]
    Say ("scene with activator Bumblebee: cinematic players during it $cine; after it (distance to Bumblebee): " + (($pl | % { "$($_.name) $([int](Dist $_ $b))" }) -join ', '))
    if ($cine -lt 1) { $fail += "the cinematic op did not start a scene" }
    elseif (@($pl | ? { $_.name -ne "Bumblebee" -and (Dist $_ $b) -gt 1200 }).Count) { $fail += "not gathered at Bumblebee after the scene" }
    Start-Sleep 3; $gv = GuestViews $b "Bumblebee"; Say "guests' own view after the scene (distance to Bumblebee): $($gv.txt)"
    if ($gv.bad.Count) { $fail += "guest view not at Bumblebee: $($gv.bad -join ',')" }
    Grid "5b_after_scene"
  }
  Say "player events seen as activators: $(@(Select-String -Path (Log A) -Pattern 'scene: event ').Count) (first: $(@(Select-String -Path (Log A) -Pattern 'scene: event ' | Select-Object -First 3 | % { $_.Line.Substring($_.Line.IndexOf('scene:')) }) -join ' | '))"
  foreach ($c in $copies) { Copy-Item (Log $c) "$Out\$c.log" -ErrorAction SilentlyContinue }
  if ($fail.Count) { Say "RESULT: FAIL $($fail -join '; ')" } else { Say "RESULT: PASS 4 players on $Map through the F10 lobby, everybody gathered at the activator" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
