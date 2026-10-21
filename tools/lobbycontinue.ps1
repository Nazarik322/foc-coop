# Lobby + the game's own "Continue" (owner 2026-10-20), driven by keys: A creates a lobby, B joins it, A presses Up +
# Enter in the main menu (CONTINUE), B must follow into the mission; then Esc on the host (pause menu) and the game must
# stay in the map. Screenshots of both windows in $Out. Leaves the games running with -Keep.
# -Keys: the menu path; "UP,ENTER" = CONTINUE (default), "ENTER,ENTER,ENTER" = CAMPAIGN > chapter I > overwrite warning Continue
param([string]$Out = "E:\FoC-coop\sessions\lobbycont", [switch]$Keep, [string]$Keys = "UP,ENTER")
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Shots($tag) { foreach ($c in "A", "B") { $f = "$Out\$tag`_$c.bmp"; Send-Cmd $c "shot $f"; Start-Sleep 1.5; if (Test-Path $f) { $i = [Drawing.Image]::FromFile($f); $i.Save(($f -replace '\.bmp$', '.png'), [Drawing.Imaging.ImageFormat]::Png); $i.Dispose(); Remove-Item $f } } }
function St($c) { Read-FocState $c }
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  Stop-Foc; Stop-Relay
  Remove-Item E:\FoC-coop\A\Binaries\foc_relay\relay.cmd, E:\FoC-coop\B\Binaries\foc_relay\relay.cmd -ErrorAction SilentlyContinue
  Remove-Item E:\FoC-coop\A\Binaries\foc_relay\relay.status, E:\FoC-coop\B\Binaries\foc_relay\relay.status -ErrorAction SilentlyContinue   # an old status gave a stale lobby id
  $pa = Start-Foc "A" "" 960 540; [void](Wait-FocWindow $pa 90)
  $pb = Start-Foc "B" "" 960 540; [void](Wait-FocWindow $pb 90)
  # wait until both games sit in the main menu (a fixed 30 s was too short once the window took 59 s to appear)
  foreach ($c in "A", "B") { if (-not (Wait-FocState $c { param($s) $s.map -like "UI_FrontEnd*" } 240)) { Say "RESULT: FAIL $c never reached the main menu"; return } }
  # and until both have their D3D window (it came ~60 s later while the remote desktop was not shown)
  foreach ($c in "A", "B") { $ok = $false; for ($i = 0; $i -lt 240 -and -not $ok; $i++) { Start-Sleep 1; $ok = [bool](Select-String -Path "E:\FoC-coop\$c\Binaries\foc_coop.log" -Pattern "overlay initialised" -Quiet) }; if (-not $ok) { Say "RESULT: SKIP $c has no game window after 240 s (remote desktop not shown?)"; return } }
  Start-Sleep 8
  Send-Cmd A "lobby create 4"
  $id = $null; for ($i = 0; $i -lt 30 -and -not $id; $i++) { Start-Sleep 1; $st = Get-Content "E:\FoC-coop\A\Binaries\foc_relay\relay.status" -ErrorAction SilentlyContinue; if (($st -match "^state=hosting")) { $id = ($st -match "^lobby=") -replace "lobby=", "" } }
  if (-not $id) { Say "RESULT: FAIL host lobby not created"; return }
  Send-Cmd B "lobby join $id"; Start-Sleep 7
  Say "lobby $id, guest relay: $(((Get-Content E:\FoC-coop\B\Binaries\foc_relay\relay.status) -match '^state='))"
  foreach ($k in $Keys -split ",") { Send-Cmd A "input tap $k"; Start-Sleep 5 }   # the overwrite warning needs time to open
  Say "host: menu keys $Keys pressed"
  $t0 = Get-Date; $ok = $false
  while (((Get-Date) - $t0).TotalSeconds -lt 200) {
    Start-Sleep 5
    $a = St A; $b = St B
    if ($b.map -like "L01*" -and $b.role -eq "client") { $ok = $true; break }
  }
  Say "after $([int]((Get-Date) - $t0).TotalSeconds) s: host $($a.map)/$($a.role)/port $($a.port)/players $(@($a.players).Count), guest $($b.map)/$($b.role)"
  Shots "1_after_continue"
  if (-not $ok) { Say "RESULT: FAIL the guest did not follow"; return }
  Start-Sleep 20
  Send-Cmd A "input tap ESC"; Start-Sleep 4; Shots "2_host_esc"
  $a = St A; Say "after Esc: host $($a.map) alive $([bool](Get-Process -Id $pa -ErrorAction SilentlyContinue))"
  Send-Cmd A "input tap ESC"; Start-Sleep 3
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" "$Out\$c.log" -ErrorAction SilentlyContinue }
  if ($a.map -like "L01*") { Say "RESULT: PASS guest followed the host's CONTINUE, host survived Esc" } else { Say "RESULT: FAIL host left the map after Esc ($($a.map))" }
} finally {
  if (-not $Keep) { Stop-Foc; Stop-Relay }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
