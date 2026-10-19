# Map matrix: for each map start a host (copy A) and a guest (copy B), pick characters, and record what happens.
param(
  [string[]]$Maps = @("ESC_Ark_BASE_m"),
  [string]$Out = "$env:TEMP\foc_matrix",
  [int]$Players = 2
)
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
$report = Join-Path $Out "report.txt"
function Count-Exc($copy) {
  $f = "E:\FoC-coop\$copy\Binaries\foc_coop.log"
  if (-not (Test-Path $f)) { return -1 }
  (Select-String -Path $f -Pattern "EXCEPTION c0000005 at [0-9A-F]+ in .*TFOC.exe" | Measure-Object).Count
}
foreach ($map in $Maps) {
  Stop-Foc; Start-Sleep 3
  $isStory = $map -like "L*"
  $game = if ($isStory) { "TnCampaignGame" } else { "TnSurvivalGame" }
  $url = "$map`?Game=TransContent.$game`?listen`?MaxPlayers=$Players"
  $line = "== $map"
  $pidA = Start-Foc "A" $url 960 540
  $hA = Wait-FocWindow $pidA 90
  $loadedA = $false
  for ($i = 0; $i -lt 80; $i++) { Start-Sleep 2; if (-not (Get-Process -Id $pidA -ErrorAction SilentlyContinue)) { break }; if (Select-String -Path "E:\FoC-coop\A\Binaries\foc_coop.log" -Pattern "pcdiag: PC " -Quiet) { $loadedA = $true; break } }
  Start-Sleep 5
  $alive = [bool](Get-Process -Id $pidA -ErrorAction SilentlyContinue)
  if ($alive) { [W]::Click($hA, 6, 6); Start-Sleep 1; Shot $hA "$Out\$map`_A_loaded.png" | Out-Null }
  $line += " | host loaded=$loadedA alive=$alive"
  if ($alive) {
    $pidB = Start-Foc "B" "127.0.0.1:7778" 960 540
    $hB = Wait-FocWindow $pidB 90
    Start-Sleep 45
    $aliveB = [bool](Get-Process -Id $pidB -ErrorAction SilentlyContinue)
    if ($aliveB) { [W]::Click($hB, 6, 6); Start-Sleep 1; Shot $hB "$Out\$map`_B_joined.png" | Out-Null; [W]::Key(0x0D) }
    FocusWin $hA; [W]::Key(0x0D); Start-Sleep 20
    Shot $hA "$Out\$map`_A_play.png" | Out-Null
    if ($aliveB) { FocusWin $hB; Shot $hB "$Out\$map`_B_play.png" | Out-Null }
    Start-Sleep 20
    $aliveA2 = [bool](Get-Process -Id $pidA -ErrorAction SilentlyContinue); $aliveB2 = [bool](Get-Process -Id $pidB -ErrorAction SilentlyContinue)
    $ctl = (Select-String -Path "E:\FoC-coop\A\Binaries\foc_coop.log" -Pattern "pcdiag: (\d+) controllers" | Select-Object -Last 1).Matches.Groups[1].Value
    $line += " | guest alive=$aliveB host controllers=$ctl | after 40s: hostAlive=$aliveA2 guestAlive=$aliveB2 excA=$(Count-Exc A) excB=$(Count-Exc B)"
  }
  Add-Content $report $line
  $line
  Stop-Foc
}
