# Escalation with 4 players (owner 2026-10-21): only connection, names and what the owner looks at - no movement, no
# shooting, no test actions. Copies A (host) + B, C, D join by IP. Windows 800x450 in a 2x2 grid (the DLL keeps each
# window in its slot), so the owner sees all four. Every copy gets an invented test name (ini playername): the TAB
# board must show four different names from the start. The games stay open unless -Stop.
param([string]$Map = "ESC_Ark_BASE_m", [string[]]$Copies = @("A", "B", "C", "D"), [string]$Out = "E:\FoC-coop\sessions\esc4",
      [switch]$Stop, [switch]$NoBuild)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "esc4.txt") $l; $l }
$names = @{ A = "Optimus"; B = "Bumblebee"; C = "Ironhide"; D = "Ratchet" }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" })
  if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  foreach ($c in $Copies) { Set-IniKey $c "playername" $names[$c]; if ($c -ne "A") { Set-IniKey $c "instance" $c } }
  if (-not $NoBuild) { Build-Deploy -Repo (Split-Path -Parent $PSScriptRoot) -Copies $Copies -NoSlot | Out-Null }
  Stop-Foc; Stop-Relay; foreach ($c in $Copies) { Clear-FocState $c }
  $opts = "?GameModeTag=SV?GameTeamStatus=2?GameRules=TransGame.TnGameRules_CompetitiveScoring"
  $pids = @{}
  $pids["A"] = Start-Foc "A" "$Map`?Game=TransContent.TnSurvivalGame$opts`?listen`?MaxPlayers=$($Copies.Count)" 800 450
  $sA = Wait-FocState A { param($s) $s.map -eq $Map -and $s.role -eq "host" -and $s.port -gt 0 } 240
  if (-not $sA) { Say "RESULT: FAIL host not in $Map"; return }
  Say "host in $Map port $($sA.port)"
  foreach ($c in $Copies | Where-Object { $_ -ne "A" }) {
    $pids[$c] = Start-Foc $c "127.0.0.1:$($sA.port)" 800 450
    $s = Wait-FocState $c { param($s) $s.map -eq $Map -and $s.role -eq "client" } 240
    Say "$c ($($names[$c])) joined: $([bool]$s)"
  }
  Start-Sleep 25
  # character pick, keys inside each window (no focus change): guests Enter, host Right + Enter (B3: default locked)
  # every player needs another character (a taken one does nothing): B the first, host the second (B3: its default
  # is locked), C the third, D the fourth
  $rights = @{ B = 0; C = 2; D = 3 }
  foreach ($c in $Copies | Where-Object { $_ -ne "A" }) { for ($r = 0; $r -lt $rights[$c]; $r++) { Send-Cmd $c "input tap RIGHT"; Start-Sleep 1.5 }; Send-Cmd $c "input tap ENTER"; Start-Sleep 2 }
  Send-Cmd A "input tap RIGHT"; Start-Sleep 1.5; Send-Cmd A "input tap ENTER"
  $ok = Wait-FocState A { param($s) $s.players.Count -ge $Copies.Count -and -not ($s.players | Where-Object { -not $_.pawn }) } 60
  if (-not $ok) { foreach ($c in $Copies | Where-Object { $_ -ne "A" }) { Send-Cmd $c "input tap ENTER"; Start-Sleep 2 }; Send-Cmd A "input tap ENTER"; Start-Sleep 20 }
  Start-Sleep 10
  $a = Read-FocState A
  $got = @($a.players | ForEach-Object { $_.name })
  Say "host sees $(@($a.players).Count) players, pawns $(@($a.players | Where-Object { $_.pawn }).Count), names: $($got -join ', ')"
  # TAB board on the host (screenshot while TAB is held)
  Send-Cmd A "input hold TAB 4000"; Start-Sleep 2; Send-Cmd A "shot $Out\tab_A.bmp"; Start-Sleep 3
  if (Test-Path "$Out\tab_A.bmp") { $i = [Drawing.Image]::FromFile("$Out\tab_A.bmp"); $i.Save("$Out\tab_A.png", [Drawing.Imaging.ImageFormat]::Png); $i.Dispose(); Remove-Item "$Out\tab_A.bmp" }
  $want = @($Copies | ForEach-Object { $names[$_] })
  $missing = @($want | Where-Object { $got -notcontains $_ })
  $dup = @($got | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
  if (-not $missing.Count -and -not $dup.Count) { Say "RESULT: PASS $($Copies.Count) players with their own names" } else { Say "RESULT: FAIL names missing [$($missing -join ',')] duplicates [$($dup -join ',')]" }
} finally {
  if ($Stop) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
