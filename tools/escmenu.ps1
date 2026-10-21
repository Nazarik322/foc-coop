# Escalation through the game's own main menu (owner 2026-10-21), one copy: ESCALATION -> party lobby (PartyHost) ->
# Private Match -> Create Game (GameHost) [-> Start Game: the Escalation map loads (TnSurvivalGame)]. Keys are sent
# into the window (input hold, no focus change); screenshots go to $Out. No player actions in the map.
# Twin: -Twin sets esconline=0 in A's ini - the menu must stop at "Sign in / Failed to sign in" (no party lobby).
param([string]$Copy = "A", [string]$Out = "E:\FoC-coop\sessions\escmenu_test", [switch]$Start, [switch]$Twin, [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "escmenu.txt") $l; $l }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" })
  if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
$log = "E:\FoC-coop\$Copy\Binaries\foc_coop.log"
function Keys([string[]]$k, [string]$tag) {
  foreach ($x in $k) { if ($x -like "WAIT*") { Start-Sleep ([int]$x.Substring(4)); continue }; Send-Cmd $Copy "input hold $x 20"; Start-Sleep 2 }
  $f = "$Out\$tag.bmp"; Send-Cmd $Copy "shot $f"; Start-Sleep 3
  if (Test-Path $f) { $i = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 960, 540; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($i, 0, 0, 960, 540); $b.Save("$Out\$tag.png", [Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $b.Dispose(); $i.Dispose(); Remove-Item $f }
}
function Seen($pattern) { [bool](Select-String -Path $log -Pattern $pattern -Quiet) }
Enter-FocSlot -Owner (Get-FocSlotOwner "")
try {
  Set-IniKey $Copy "esconline" ([int](-not $Twin))
  Stop-Foc
  $p = Start-Foc $Copy "" 960 540; [void](Wait-FocWindow $p 90)
  for ($i = 0; $i -lt 240; $i++) { Start-Sleep 1; if (Select-String -Path $log -Pattern "overlay initialised" -Quiet) { break } }
  Start-Sleep 15
  Keys @("DOWN", "DOWN", "ENTER", "WAIT25") "1_escalation"
  $party = Seen "esclogin: game TnLobbyGame PartyHost"
  Say "party lobby (PartyHost): $party"
  if ($Twin) {
    if ($party) { Say "RESULT: FAIL twin reached the party lobby without the online patches" } else { Say "RESULT: PASS twin (no party lobby without esconline)" }
    return
  }
  if (-not $party) { Say "RESULT: FAIL no party lobby"; return }
  Keys @("DOWN", "ENTER", "WAIT6", "ENTER", "WAIT25") "2_create_game"
  $gameHost = Seen "esclogin: game TnLobbyGame GameHost"
  Say "game lobby (GameHost): $gameHost"
  if (-not $gameHost) { Say "RESULT: FAIL no game lobby after Create Game"; return }
  if ($Start) {
    Keys @("ENTER", "WAIT40") "3_start_game"
    $map = Seen "esclogin: game TnSurvivalGame"
    Say "Escalation map (TnSurvivalGame): $map"
    if (-not $map) { Say "RESULT: FAIL map did not load after Start Game"; return }
  }
  Say "RESULT: PASS escalation menu ($(if ($Start) { 'party, game lobby, map' } else { 'party, game lobby' }))"
} finally {
  Set-IniKey $Copy "esconline" 1
  if (-not $Keep) { Stop-Foc }
  Exit-FocSlot -Owner (Get-FocSlotOwner "")
}
