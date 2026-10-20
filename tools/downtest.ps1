# B11 test (downed players): the host's health is set to 1 (`hp` command) and it walks forward shooting (real input,
# needs the desktop shown) until the chapter 6 enemies down it. Expected: the host stays downed (does not bleed out, no game over) for 45 s. Then the guest is downed
# too: with everybody down the game must end in MISSION FAILED with "Restart From Last Checkpoint" (screenshots).
# Twin: -Twin writes coopdeath=0 into A's ini; the downed host then bleeds out (FAIL).
param([string]$Map = "L06_Megatron_BASE_m", [string]$Out = "", [switch]$Twin, [int]$WaitDown = 90, [string]$SlotOwner = "", [switch]$NoSlot)
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
Add-Type -AssemblyName System.Drawing
if (-not $Out) { $Out = "E:\FoC-coop\sessions\down_$(Get-Date -Format yyyyMMdd_HHmm)_$Map" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "down.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
function P($s, [bool]$local) { $s.players | Where-Object { [bool]$_.local -eq $local } }
function Shot($tag) {
  foreach ($c in "A", "B") { $f = Join-Path $Out "$tag`_$c.bmp"; Send-Cmd $c "shot $f"; Start-Sleep 1.5
    if (Test-Path $f) { try { $im = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 640, 357; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($im, 0, 0, 640, 357); $b.Save(($f -replace '\.bmp$', '.jpg'), [Drawing.Imaging.ImageFormat]::Jpeg); $g.Dispose(); $b.Dispose(); $im.Dispose(); Remove-Item $f } catch { } } }
}
$ini = "E:\FoC-coop\A\Binaries\foc_coop.ini"; $iniOrig = Get-Content $ini -Raw
if ($Twin) { Set-Content $ini ($iniOrig.TrimEnd() + "`r`ncoopdeath=0`r`n") -Encoding ascii }
try {
  $m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
  Say "start: $m"
  if ($m -notlike "RESULT: PASS*") { Say "RESULT: SKIP $Map cannot start"; return }
  $pa = (Find-CopyProc A).Id; $pb = (Find-CopyProc B).Id
  $hA = [W]::Find([uint32]$pa); $hB = [W]::Find([uint32]$pb)
  $fails = @()
  # 1. host downed by the enemies
  $down = $false
  for ($i = 0; $i -lt $WaitDown -and -not $down; $i += 3) {
    Send-Cmd A "hp local 1"; Start-Sleep 1
    HoldKey $hA 0x11 2000; FocusWin $hA; for ($k = 0; $k -lt 4; $k++) { [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero) }
    $h = P (Read-FocState A) $true
    if ($h.downed -eq 1) { $down = $true }
  }
  Say "host downed: $down"
  if (-not $down) { Say "RESULT: SKIP $Map the enemies did not down the host"; return }
  Shot "1_host_downed"
  Start-Sleep 45
  $s = Read-FocState A; $h = P $s $true
  Say "after 45 s: host pawn=$($h.pawn) downed=$($h.downed) hp=$($h.hp) state=$($h.state) skipped=$($s.gameOverSkipped)"
  Shot "2_host_after45"
  if (-not $h.pawn -or $h.downed -ne 1) { $fails += "downed host did not stay down 45 s (pawn=$($h.pawn) downed=$($h.downed))" }
  # 2. guest downed too -> everybody down -> mission failed
  $gdown = $false
  for ($i = 0; $i -lt $WaitDown -and -not $gdown; $i += 3) {
    Send-Cmd A "hp remote 1"; Start-Sleep 1
    HoldKey $hB 0x11 2000; FocusWin $hB; for ($k = 0; $k -lt 4; $k++) { [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero) }
    $g = P (Read-FocState A) $false
    if ($g.downed -eq 1 -or -not $g.pawn) { $gdown = $true }
  }
  Say "guest downed: $gdown"
  Start-Sleep 12
  Shot "3_all_down"
  $s = Read-FocState A
  Say "after all down: players=$(($s.players | ForEach-Object { "$($_.pc):$($_.pawn):$($_.state):d$($_.downed)" }) -join ' ')"
  if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue)) { $fails += "host died" }
  if (-not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { $fails += "guest died" }
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  if ($fails) { Say "RESULT: FAIL $Map $($fails -join '; ')" } else { Say "RESULT: PASS $Map downed host waited for a revive; check 3_all_down_*.jpg for MISSION FAILED" }
} finally {
  Set-Content $ini $iniOrig -NoNewline -Encoding ascii
  Stop-Foc; Stop-Relay
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
