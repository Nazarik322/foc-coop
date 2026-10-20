# B16 check: chapter 6 (L06), host + guest. The host's level start shows the chapter title ("CHAPTER 10 ..."); the guest
# must show it too (chaptersync, story_title.inc). Takes in-game screenshots of both copies every 2 s from the moment the
# host leaves the "waiting for the guest" pause (also the B17 observation: character animation in the intro).
# Pass = the host logged its SetChapter, the guest logged that it showed the same chapter once; screenshots are in $Out
# (look at the title in host_*.png / guest_*.png). -Twin = chaptersync=0 in both inis: must FAIL.
param([string]$Map = "L06_Megatron_BASE_m", [string]$Out = "E:\FoC-coop\sessions\title\t1", [int]$ShotSec = 60,
      [switch]$Twin)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$binA = "E:\FoC-coop\A\Binaries"; $binB = "E:\FoC-coop\B\Binaries"
$iniA = "$binA\foc_coop.ini"; $iniB = "$binB\foc_coop.ini"
$textA = Get-Content $iniA -Raw; $textB = Get-Content $iniB -Raw
function Alive($procId) { [bool](Get-Process -Id $procId -ErrorAction SilentlyContinue) }
function Finish($line) { Set-Content $iniA $textA -Encoding ascii -NoNewline; Set-Content $iniB $textB -Encoding ascii -NoNewline; $line; Stop-Foc }

$noDisplay = $true
for ($i = 0; $i -lt 20 -and $noDisplay; $i++) { if ([W]::GetForegroundWindow() -ne [IntPtr]::Zero) { $noDisplay = $false } else { Start-Sleep -Milliseconds 500 } }
if ($noDisplay) { "RESULT: SKIP titletest needs the RDP window shown (no display)"; return }

if ($Twin) {
  Set-Content $iniA ($textA.TrimEnd() + "`r`nchaptersync=0`r`n") -Encoding ascii -NoNewline
  Set-Content $iniB ($textB.TrimEnd() + "`r`nchaptersync=0`r`n") -Encoding ascii -NoNewline
}
Stop-Foc; Stop-Relay; Start-Sleep 3
Clear-FocState A; Clear-FocState B
foreach ($f in "$binA\foc_coop.log", "$binB\foc_coop.log") { try { [IO.File]::Delete($f) } catch {} }
$game = "TnCampaignGame"
$pidA = Start-Foc "A" "$Map`?Game=TransContent.$game`?listen`?MaxPlayers=2" 960 540
$sA = Wait-FocState A { param($s) $s.map -eq $Map -and $s.role -eq "host" -and $s.port -gt 0 } 180
if (-not $sA) { Finish "RESULT: FAIL titletest host not in map $Map"; return }
if ($sA.game -ne $game) { Finish "RESULT: FAIL titletest host game class is $($sA.game), expected $game"; return }
$pidB = Start-Foc "B" "127.0.0.1:$($sA.port)" 960 540
$sB = Wait-FocState B { param($s) $s.map -eq $Map -and $s.role -eq "client" } 150
if (-not $sB) { Finish "RESULT: FAIL titletest guest not in map $Map"; return }
$sA = Wait-FocState A { param($s) $s.players.Count -ge 2 } 60
if (-not $sA) { Finish "RESULT: FAIL titletest host does not see the guest"; return }
"mode: map=$($sA.map) game=$($sA.game) host=$($sA.role) guest=$($sB.role) players=$($sA.players.Count)"

# wait for the level start (host unpaused after the guest is ready), then shoot every 2 s
$t0 = Get-Date; $start = $null
while (((Get-Date) - $t0).TotalSeconds -lt 120 -and -not $start) {
  if (Select-String -Path "$binA\foc_coop.log" -Pattern "waitguest: .*unpaused" -Quiet) { $start = Get-Date } else { Start-Sleep -Milliseconds 300 }
}
if (-not $start) { Finish "RESULT: FAIL titletest the host never left the waiting pause"; return }
"level start at $(Get-Date $start -Format HH:mm:ss.fff)"
$n = 0
while (((Get-Date) - $start).TotalSeconds -lt $ShotSec) {
  $sec = [int]((Get-Date) - $start).TotalSeconds
  Send-Cmd A "shot $Out\host_$('{0:D2}' -f $n)_t$sec.bmp"; Send-Cmd B "shot $Out\guest_$('{0:D2}' -f $n)_t$sec.bmp"
  $n++; Start-Sleep -Milliseconds 2000
}
Start-Sleep 2
foreach ($f in (Get-ChildItem -Path $Out -Filter *.bmp)) {
  $im = [Drawing.Image]::FromFile($f.FullName); $im.Save([IO.Path]::ChangeExtension($f.FullName, 'png'), [Drawing.Imaging.ImageFormat]::Png); $im.Dispose(); [IO.File]::Delete($f.FullName)
}
Copy-Item "$binA\foc_coop.log" "$Out\host.log"; Copy-Item "$binB\foc_coop.log" "$Out\guest.log"
$hostLines = @(Select-String -Path "$Out\host.log" -Pattern "title: host" | ForEach-Object { $_.Line })
$guestLines = @(Select-String -Path "$Out\guest.log" -Pattern "title: guest" | ForEach-Object { $_.Line })
"host log:"; $hostLines; "guest log:"; $guestLines
# the screen decides: white pixels of the "CHAPTER 10" text (x 785-880, y 470-498 at 1920x1080) in any frame
function TitleScore([string]$Prefix) {
  $best = 0
  foreach ($f in (Get-ChildItem -Path $Out -Filter "${Prefix}_*.png")) {
    $i = New-Object Drawing.Bitmap $f.FullName; $s = $i.Width / 1920.0; $c = 0
    for ($y = [int](470 * $s); $y -lt [int](498 * $s); $y++) { for ($x = [int](785 * $s); $x -lt [int](880 * $s); $x++) {
      $p = $i.GetPixel($x, $y); if ($p.R -gt 215 -and $p.G -gt 215 -and $p.B -gt 215) { $c++ } } }
    $i.Dispose(); if ($c -gt $best) { $best = $c }
  }
  return $best
}
$hostScore = TitleScore "host"; $guestScore = TitleScore "guest"
"screen: title pixels host max $hostScore, guest max $guestScore (need >= 100 on both)"
$alive = (Alive $pidA) -and (Alive $pidB)
$res = $null
if (-not $alive) { $res = "a game died during the intro" }
elseif ($hostLines.Count -lt 1) { $res = "the host never reported its chapter title" }
elseif ($guestLines.Count -lt 1) { $res = "the guest never showed the chapter title (chaptersync)" }
elseif ($hostScore -lt 100) { $res = "the host screenshots show no chapter title (score $hostScore)" }
elseif ($guestScore -lt 100) { $res = "the guest screenshots show no chapter title (score $guestScore)" }
$n10 = @($guestLines | Where-Object { $_ -match "shows chapter" }).Count
if (-not $res -and $n10 -lt 1) { $res = "no 'shows chapter' line on the guest" }
if ($res) { Finish "RESULT: FAIL titletest $res (screenshots in $Out)" }
else { Finish "RESULT: PASS titletest host $($hostLines.Count) title(s), guest showed $n10, screen host/guest $hostScore/$guestScore (screenshots in ${Out}: compare host_*.png and guest_*.png)" }
