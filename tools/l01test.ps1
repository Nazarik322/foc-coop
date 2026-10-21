# Chapter 1 mirror test (owner 2026-10-20: the guest must repeat what the script does to the host).
# Starts L01 for host (A) and guest (B), then for $Sec seconds every $Every s: screenshots of both windows and one line
# of state (positions, distance, cinematic mode, hidden) from both state files. At the end: mirror / tpfollow / rescue
# lines of both logs and a verdict. Screenshots: $Out\NNN_t<sec>_A.png / _B.png, side by side in $Out\grid_*.jpg.
# Usage: l01test.ps1 [-Sec 180] [-Every 10] [-Twin] (Twin: mirror=0 on both)
param([string]$Map = "L01_TheExodus_BASE_m", [string]$Out = "", [int]$Sec = 180, [int]$Every = 10, [switch]$Twin,
  [string]$SlotOwner = "", [switch]$NoSlot)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
if (-not $Out) { $Out = "E:\FoC-coop\sessions\l01_$(Get-Date -Format yyyyMMdd_HHmm)" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "l01.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
$inis = @{}
foreach ($c in "A", "B") { $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"; $inis[$p] = Get-Content $p -Raw }
if ($Twin) { foreach ($p in $inis.Keys) { Set-Content $p ($inis[$p].TrimEnd() + "`r`nmirror=0`r`n") -Encoding ascii } }
function P($s, [bool]$local) { $s.players | Where-Object { [bool]$_.local -eq $local } | Select-Object -First 1 }
function D($a, $b) { if (-not $a -or -not $b -or -not $a.pawn -or -not $b.pawn) { return -1 }; [int][math]::Sqrt([math]::Pow($a.x - $b.x, 2) + [math]::Pow($a.y - $b.y, 2) + [math]::Pow($a.z - $b.z, 2)) }
try {
  $m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
  Say "start: $m"
  if ($m -notlike "RESULT: PASS*") { Say "RESULT: FAIL $Map cannot start"; return }
  $pa = (Find-CopyProc A).Id; $pb = (Find-CopyProc B).Id
  $t0 = Get-Date; $n = 0; $far = 0; $cineMismatch = 0; $samples = 0
  while (((Get-Date) - $t0).TotalSeconds -lt $Sec) {
    $el = [int]((Get-Date) - $t0).TotalSeconds
    $tag = "{0:D3}_t{1}" -f $n, $el
    Send-Cmd A "shot $Out\${tag}_A.bmp"; Start-Sleep -Milliseconds 400; Send-Cmd B "shot $Out\${tag}_B.bmp"
    $a = Read-FocState A; $b = Read-FocState B
    if ($a -and $b) {
      $h = P $a $true; $g = P $a $false; $gb = P $b $true
      $dist = D $h $g
      $line = "t=$el host($($h.x),$($h.y),$($h.z)) cine=$($h.cine) hid=$($h.hidden) | guest@host($($g.x),$($g.y),$($g.z)) | guest own cine=$($gb.cine) hid=$($gb.hidden) | dist=$dist"
      Say $line
      $samples++
      if ($dist -gt 3000) { $far++ }
      if ($h.cine -ne $gb.cine) { $cineMismatch++ }
    }
    if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue) -or -not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { Say "a game process ended"; break }
    $n++; Start-Sleep -Seconds ([math]::Max(1, $Every - 1))
  }
  Start-Sleep 2
  foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  # png + side-by-side grids (host left, guest right)
  foreach ($f in (Get-ChildItem $Out -Filter *.bmp)) { try { $im = [Drawing.Image]::FromFile($f.FullName); $im.Save([IO.Path]::ChangeExtension($f.FullName, 'png'), [Drawing.Imaging.ImageFormat]::Png); $im.Dispose(); Remove-Item $f.FullName } catch {} }
  $pairs = Get-ChildItem $Out -Filter "*_A.png" | Sort-Object Name
  for ($i = 0; $i -lt $pairs.Count; $i += 4) {
    $g = New-Object Drawing.Bitmap 1280, 1440; $gr = [Drawing.Graphics]::FromImage($g)
    for ($k = 0; $k -lt 4 -and $i + $k -lt $pairs.Count; $k++) {
      $fa = $pairs[$i + $k].FullName; $fb = $fa -replace '_A\.png$', '_B.png'
      foreach ($pair in @(@($fa, 0), @($fb, 640))) { if (Test-Path $pair[0]) { $im = [Drawing.Image]::FromFile($pair[0]); $gr.DrawImage($im, $pair[1], $k * 360, 640, 360); $im.Dispose() } }
    }
    $g.Save((Join-Path $Out ("grid_{0:D2}.jpg" -f ($i / 4))), [Drawing.Imaging.ImageFormat]::Jpeg); $gr.Dispose(); $g.Dispose()
  }
  foreach ($c in "A", "B") {
    Say "--- $c log: mirror / tpfollow / rescue / exceptions"
    Select-String -Path (Join-Path $Out "$c.log") -Pattern "mirror:|tpfollow:|rescue:|tp: |EXCEPTION|cinesync" | Select-Object -First 40 | ForEach-Object { Say $_.Line }
  }
  $alive = (Get-Process -Id $pa -ErrorAction SilentlyContinue) -and (Get-Process -Id $pb -ErrorAction SilentlyContinue)
  $allp = @(Select-String -Path (Join-Path $Out "A.log") -Pattern "mirror: allplayers").Count
  $fails = @()
  if (-not $alive) { $fails += "a game died" }
  if (-not $Twin -and $allp -lt 1) { $fails += "host set no allplayers flag" }
  Say "summary: samples=$samples far(>3000uu)=$far cineMismatch=$cineMismatch allplayersLines=$allp alive=$alive"
  if ($fails) { Say "RESULT: FAIL $Map $($fails -join '; ')" } else { Say "RESULT: PASS $Map (check grid_*.jpg: host left, guest right)" }
} finally {
  foreach ($p in $inis.Keys) { Set-Content $p $inis[$p] -NoNewline -Encoding ascii }
  Stop-Foc; Stop-Relay
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
