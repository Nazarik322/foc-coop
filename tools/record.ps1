# Records a play session for later bug hunting: every $Every seconds an in-game screenshot of each running copy
# (the DLL's `shot` command, game image only) and a copy of its foc_state.json. Stops when no test copy runs.
# Output: $Out\<time>_<copy>.png / .state.json; logs are copied at the end.
param([string]$Out = "E:\FoC-coop\sessions\$(Get-Date -Format yyyyMMdd_HHmm)", [int]$Every = 20, [int]$MaxMinutes = 180)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$end = (Get-Date).AddMinutes($MaxMinutes)
while ((Get-Date) -lt $end) {
  $running = @("A", "B") | Where-Object { Find-CopyProc $_ }
  if (-not $running) { break }
  $t = Get-Date -Format HHmmss
  foreach ($c in $running) {
    $bmp = Join-Path $Out "$t`_$c.bmp"
    Send-Cmd $c "shot $bmp"
    Copy-Item "E:\FoC-coop\$c\Binaries\foc_state.json" (Join-Path $Out "$t`_$c.state.json") -ErrorAction SilentlyContinue
    Start-Sleep 2
    if (Test-Path $bmp) {
      try { $im = [Drawing.Image]::FromFile($bmp); $b = New-Object Drawing.Bitmap 960, 536; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($im, 0, 0, 960, 536)
            $b.Save(($bmp -replace '\.bmp$', '.jpg'), [Drawing.Imaging.ImageFormat]::Jpeg); $g.Dispose(); $b.Dispose(); $im.Dispose(); Remove-Item $bmp } catch { }
    }
  }
  Start-Sleep ([Math]::Max(1, $Every - 4))
}
foreach ($c in "A", "B") {
  Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue
  Get-ChildItem "E:\FoC-coop\$c\Binaries\foc_crash_*.dmp" -ErrorAction SilentlyContinue | Copy-Item -Destination $Out
}
"session saved to $Out"
