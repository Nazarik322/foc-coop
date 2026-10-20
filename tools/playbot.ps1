# Play bot: host and guest walk through a chapter with real key / mouse input (needs the desktop shown), and the run
# checks what a player would notice: both pawns exist, the guest sees the host where the host really is (and the
# other way round), no sudden teleports, no crashes, the game keeps running. Screenshots of both screens every step.
# Usage: playbot.ps1 -Map L02b_DestroyAllMortars_BASE_m -Minutes 4      (starts host A + guest B itself)
param([string]$Map = "L02b_DestroyAllMortars_BASE_m", [int]$Minutes = 4, [string]$Out = "", [switch]$Steam, [string]$SlotOwner = "", [switch]$NoSlot)
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
if (-not $Out) { $Out = "E:\FoC-coop\sessions\bot_$(Get-Date -Format yyyyMMdd_HHmm)_$Map" }
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
$log = Join-Path $Out "bot.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
function Pos($p) { if ($p) { @([double]$p.x, [double]$p.y, [double]$p.z) } else { $null } }
function D($a, $b) { if (-not $a -or -not $b) { return -1 }; [int][math]::Sqrt(($a[0]-$b[0])*($a[0]-$b[0]) + ($a[1]-$b[1])*($a[1]-$b[1]) + ($a[2]-$b[2])*($a[2]-$b[2])) }
function ShotBoth($tag) {
  foreach ($c in "A", "B") { $f = Join-Path $Out "$tag`_$c.bmp"; Send-Cmd $c "shot $f"; Start-Sleep 1.5
    if (Test-Path $f) { try { $im = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 640, 357; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($im, 0, 0, 640, 357); $b.Save(($f -replace '\.bmp$', '.jpg'), [Drawing.Imaging.ImageFormat]::Jpeg); $g.Dispose(); $b.Dispose(); $im.Dispose(); Remove-Item $f } catch { } } }
}
function Turn($h, [int]$dx) { FocusWin $h; for ($i = 0; $i -lt 10; $i++) { [W]::mouse_event(1, [int]($dx / 10), 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30 } }

# start: matrix with -Keep gives a verified host + guest with pawns
$m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep -Steam:$Steam | Select-String "RESULT" | ForEach-Object { $_.Line }
Say "start: $m"
if ($m -notlike "RESULT: PASS*") { Say "BOT: cannot start, see above"; return }
$pa = (Find-CopyProc A).Id; $pb = (Find-CopyProc B).Id
$hA = [W]::Find([uint32]$pa); $hB = [W]::Find([uint32]$pb)
$problems = @(); $step = 0; $end = (Get-Date).AddMinutes($Minutes)
$prevH = $null; $prevG = $null; $jumpH = -1; $jumpG = -1
while ((Get-Date) -lt $end) {
  $step++; Update-FocSlot
  if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue)) { $problems += "host died at step $step"; break }
  if (-not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { $problems += "guest died at step $step"; break }
  # one action per step for each player (like a player would): walk/look, sprint, shoot, aim+shoot, transform and drive,
  # strafe, jump, interact. Scan codes: W 11 A 1E S 1F D 20 Q 10 E 12 Space 39 LShift 2A.
  $actions = @("walk", "shoot", "transform", "strafe", "aimshoot", "sprint", "jump", "interact")
  foreach ($who in @(@{h = $hA; n = "host"; k = 0}, @{h = $hB; n = "guest"; k = 3})) {
    $act = $actions[($step + $who.k) % $actions.Count]; $h = $who.h
    # stuck against a wall (moved < 150 uu last step): turn around first, like a player would
    $moved = if ($who.n -eq "host") { $jumpH } else { $jumpG }
    if ($step -gt 1 -and $moved -ge 0 -and $moved -lt 150) { Turn $h (@(900, -700, 1200)[$step % 3]); Add-Content $log "$(Get-Date -Format HH:mm:ss) step $step $($who.n): stuck, turning" }
    switch ($act) {
      "walk"      { Turn $h (@(300, -300, 150, -150)[$step % 4]); HoldKey $h 0x11 2500 }
      "shoot"     { FocusWin $h; for ($i = 0; $i -lt 6; $i++) { [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 100 } }
      "aimshoot"  { FocusWin $h; [W]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400; [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep 1; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); [W]::mouse_event(16, 0, 0, 0, [UIntPtr]::Zero) }
      "transform" { HoldKey $h 0x10 150; Start-Sleep 1; HoldKey $h 0x11 2000; HoldKey $h 0x10 150 }
      "strafe"    { HoldKey $h 0x1E 1200; HoldKey $h 0x20 1200 }
      "sprint"    { FocusWin $h; [W]::keybd_event(0, 0x2A, 8, [UIntPtr]::Zero); HoldKey $h 0x11 2000; [W]::keybd_event(0, 0x2A, 10, [UIntPtr]::Zero) }
      "jump"      { HoldKey $h 0x39 150; HoldKey $h 0x11 1000 }
      "interact"  { HoldKey $h 0x12 600 }
    }
    Add-Content $log "$(Get-Date -Format HH:mm:ss) step $step $($who.n): $act"
  }
  Start-Sleep 1
  $sa = Read-FocState A; $sb = Read-FocState B
  $hostOwnH = Pos ($sa.players | Where-Object { $_.local }); $hostSeesG = Pos ($sa.players | Where-Object { -not $_.local })
  $guestOwnG = Pos ($sb.players | Where-Object { $_.local })
  # the guest sees the host's pawn as an unpossessed pawn in its own world: the one nearest to the host's real position
  $guestSeesH = $null; $best = 1e12
  foreach ($p in $sb.pawns) { if ($p.possessed) { continue }; $d = D (Pos $p) $hostOwnH; if ($d -ge 0 -and $d -lt $best) { $best = $d; $guestSeesH = Pos $p } }
  $syncH = D $hostOwnH $guestSeesH; $syncG = D $hostSeesG $guestOwnG
  $jumpH = D $prevH $hostOwnH; $jumpG = D $prevG $hostSeesG
  $line = "step $step host=$($hostOwnH -join ',') guest=$($hostSeesG -join ',') desync host=$syncH guest=$syncG moved host=$jumpH guest=$jumpG guards=$($sa.guards -join ',') exc=$($sa.excExe)/$($sb.excExe)"
  Say $line
  if (-not $hostOwnH) { $problems += "step $step host has no pawn" }
  if (-not $hostSeesG) { $problems += "step $step guest has no pawn on the host" }
  if ($syncH -gt 400) { $problems += "step $step guest sees the host $syncH uu away from where it is" }
  if ($syncG -gt 400) { $problems += "step $step host sees the guest $syncG uu away from where the guest is" }
  # running covers ~4000 uu per step; a teleport is a big jump or being lifted back up (the old rescue bug)
  $upH = if ($prevH -and $hostOwnH) { $hostOwnH[2] - $prevH[2] } else { 0 }; $upG = if ($prevG -and $hostSeesG) { $hostSeesG[2] - $prevG[2] } else { 0 }
  if ($jumpH -gt 9000 -or $upH -gt 1500) { $problems += "step $step host pawn teleported ($jumpH uu, up $([int]$upH))" }
  if ($jumpG -gt 9000 -or $upG -gt 1500) { $problems += "step $step guest pawn teleported ($jumpG uu, up $([int]$upG))" }
  if ($sa.excExe -gt 0 -or $sb.excExe -gt 0) { $problems += "step $step unguarded exception (host $($sa.excExe), guest $($sb.excExe))" }
  $prevH = $hostOwnH; $prevG = $hostSeesG
  ShotBoth ("{0:d3}" -f $step)
}
foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
if ($problems) { Say "BOT RESULT: $($problems.Count) problem(s)"; $problems | ForEach-Object { Say "  - $_" } } else { Say "BOT RESULT: no problems in $step steps" }
Stop-Foc; Stop-Relay
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
