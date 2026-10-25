# B5 (dark robots in scenes on guests): L01 intro with 2 copies (F10-style lobby through lobbytest), both look through
# the same cutscene camera; screenshots of host and guest, share of near-black pixels (max(R,G,B) < 14) in each.
# Before the fix the guest's frame had the robots as black silhouettes (~30-45 % black vs ~5 % on the host).
# PASS: guest black share <= host black share + 8 points in every shot pair. -Twin: lightfix=0 on the guest (must FAIL).
param([string]$Out = "E:\FoC-coop\sessions\lighttest", [switch]$Twin, [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" }); if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
function Black($f) {
  $b = New-Object Drawing.Bitmap $f; $n = 0; $all = 0
  for ($y = 20; $y -lt $b.Height * 0.8; $y += 6) { for ($x = 0; $x -lt $b.Width; $x += 6) { $p = $b.GetPixel($x, $y); $all++; if ([Math]::Max($p.R, [Math]::Max($p.G, $p.B)) -lt 14) { $n++ } } }
  $b.Dispose(); [Math]::Round(100.0 * $n / $all, 1)
}
Set-IniKey B "lightfix" $(if ($Twin) { 0 } else { 1 })
$fail = @()
try {
  powershell -ExecutionPolicy Bypass -Command "& '$PSScriptRoot\lobbytest.ps1' -Map L01_TheExodus_BASE_m -Out '$Out\lobby' -Keep" 2>&1 | Select-String "RESULT" | ForEach-Object { Say "lobbytest: $($_.Line)" }
  if (-not (Read-FocState B) -or (Read-FocState B).role -ne "client") { Say "RESULT: SKIP guest not in the mission"; return }
  Start-Sleep 50
  foreach ($k in 1..3) {
    foreach ($c in "A", "B") { Send-Cmd $c "shot $Out\s$k`_$c.bmp" }; Start-Sleep 4
    $ha = Black "$Out\s$k`_A.bmp"; $gb = Black "$Out\s$k`_B.bmp"
    Say "shot ${k}: black host $ha %, guest $gb %"
    if ($gb -gt $ha + 8) { $fail += "shot $k guest $gb % black vs host $ha %" }
    foreach ($c in "A", "B") { $i = [Drawing.Image]::FromFile("$Out\s$k`_$c.bmp"); $s = New-Object Drawing.Bitmap 960, 540; $g = [Drawing.Graphics]::FromImage($s); $g.DrawImage($i, 0, 0, 960, 540); $s.Save("$Out\s$k`_$c.png"); $g.Dispose(); $s.Dispose(); $i.Dispose(); Remove-Item "$Out\s$k`_$c.bmp" }
    Start-Sleep 12
  }
  $n = @(Select-String "E:\FoC-coop\B\Binaries\foc_coop.log" -Pattern "lightfix: \d+ part mesh").Count; Say "guest lightfix passes: $n"
  if ($fail.Count) { Say "RESULT: FAIL dark robots on the guest (B5): $($fail -join '; ')" } else { Say "RESULT: PASS guest frames as bright as the host's (B5)" }
} finally {
  Set-IniKey B "lightfix" 1
  if (-not $Keep) { Stop-Foc; Stop-Relay }
}
