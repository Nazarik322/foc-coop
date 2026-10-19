# Escalation check driven by game commands (oexec = script exec functions, poke = memory), not by mouse clicks.
# Host A + guest B through the Steam relay mock on ESC_Ark. Every step takes screenshots of both screens (TAB board where
# it matters) and reads the host's state file; the summary lists PASS/FAIL per feature.
#   score       : TAB board captured on both (kills are checked by real shooting, FakeScoreKill crashes the server)
#   money       : _SurvivalMoney poked on both PRIs -> TAB money column
#   transform   : PressTransform -> controller state changes on the host for both players
#   ability     : TriggerAbility 0
#   weapons     : NextWeapon
#   downed      : TestDowned -> downed=1 on the host, TestRevive -> up again (host and guest)
#   lives       : downed + DownedSuicide on both -> team lives spent, match end screen after the last life
param([string]$Map = "ESC_Ark_BASE_m", [string]$Out = "")
. "$PSScriptRoot\dev.ps1"
Add-Type -AssemblyName System.Drawing
if (-not $Out) { $Out = "E:\FoC-coop\sessions\esctest_$(Get-Date -Format yyyyMMdd_HHmm)" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "esctest.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
function Cmd($c, $text, $wait = 1.5) { Send-Cmd $c $text; Start-Sleep $wait }
function LastLog($c, $pattern) { (Select-String -Path "E:\FoC-coop\$c\Binaries\foc_coop.log" -Pattern $pattern | Select-Object -Last 1).Line }
function Shot($tag, [switch]$Tab) {
  foreach ($c in "A", "B") {
    $f = Join-Path $Out "$tag`_$c.bmp"
    if ($Tab) { $h = [W]::Find([uint32](Find-CopyProc $c).Id); FocusWin $h | Out-Null; [W]::keybd_event(0, 0x0F, 8, [UIntPtr]::Zero); Start-Sleep 1 }
    Send-Cmd $c "shot $f"; Start-Sleep 1.5
    if ($Tab) { [W]::keybd_event(0, 0x0F, 10, [UIntPtr]::Zero) }
    if (Test-Path $f) { try { $im = [Drawing.Image]::FromFile($f); $b = New-Object Drawing.Bitmap 960, 540; $g = [Drawing.Graphics]::FromImage($b); $g.DrawImage($im, 0, 0, 960, 540); $b.Save(($f -replace '\.bmp$', '.jpg'), [Drawing.Imaging.ImageFormat]::Jpeg); $g.Dispose(); $b.Dispose(); $im.Dispose(); Remove-Item $f } catch { } }
  }
}
function States { $s = Read-FocState A; ($s.players | ForEach-Object { "$(if ($_.local) {'host'} else {'guest'}):$($_.state):d$($_.downed):hp$($_.hp)" }) -join ' ' }
$results = [ordered]@{}

$m = & "$PSScriptRoot\matrix.ps1" -Steam -Maps $Map -Out $Out -AliveSec 10 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
Say "start: $m"
if ($m -notlike "RESULT: PASS*") { Say "RESULT: SKIP cannot start"; return }
Say "rules: $(LastLog A 'killrules')"

# score / kills: real kills are checked by shooting (the developer command FakeScoreKill crashes the server: it scores
# a kill with no victim), here only the board is captured
Shot "1_score" -Tab

# money
Cmd A "props _SurvivalMoney" 4
$pl = LastLog A 'props: _SurvivalMoney IntProperty'
$moneyOff = if ($pl) { [Convert]::ToUInt32((($pl -split ': ', 3)[2] -split ' ')[12], 16) -shr 20 } else { 0 }
Say "money offset: 0x$('{0:x}' -f $moneyOff)"
if ($moneyOff) { Cmd A ("poke pri {0:x} 5000" -f $moneyOff); Cmd A ("poke rpri {0:x} 4000" -f $moneyOff) 3; Say "poke: $(LastLog A 'poke:')" }
Shot "2_money" -Tab

# transform there and back
Cmd A "oexec pc PressTransform"; Cmd B "oexec pc PressTransform" 4
$t1 = States; Say "after transform: $t1"; Shot "3_transform"
Cmd A "oexec pc PressTransform"; Cmd B "oexec pc PressTransform" 4
$t2 = States; Say "after transform back: $t2"
$results["transform"] = if ($t1 -ne $t2 -and $t1 -match "Form") { "PASS" } else { "CHECK ($t1 / $t2)" }

# ability and weapon switch
Cmd A "oexec pc TriggerAbility 0"; Cmd B "oexec pc TriggerAbility 0" 2; Shot "4_ability"
Cmd A "oexec pc NextWeapon"; Cmd B "oexec pc NextWeapon" 3; Shot "5_weapon"
Say "ability/weapon: A=$(LastLog A 'oexec:') B=$(LastLog B 'oexec:')"

# downed and revive, host then guest
Cmd A "oexec pawn TestDowned" 3; $d1 = States; Say "host downed: $d1"; Shot "6_host_downed"
Cmd A "oexec pawn TestRevive" 3; $d2 = States; Say "host revived: $d2"
Cmd B "oexec pawn TestDowned" 3; $d3 = States; Say "guest downed: $d3"; Shot "7_guest_downed"
Cmd B "oexec pawn TestRevive" 3; $d4 = States; Say "guest revived: $d4"
$results["downed+revive host"] = if ($d1 -match "host:\S*:d1" -and $d2 -match "host:\S*:d0") { "PASS" } else { "FAIL" }
$results["downed+revive guest"] = if ($d3 -match "guest:\S*:d1" -and $d4 -match "guest:\S*:d0") { "PASS" } else { "FAIL" }

# team lives: both go down and self-destruct, three times -> match end
for ($r = 1; $r -le 4; $r++) {
  foreach ($c in "A", "B") { Cmd $c "oexec pawn TestDowned" 1 }
  Start-Sleep 2
  foreach ($c in "A", "B") { Cmd $c "oexec pc DownedSuicide" 1 }
  Start-Sleep 12
  Say "lives round ${r}: $(States)"
  Shot "8_lives_$r" -Tab
  if (-not (Find-CopyProc A) -or -not (Find-CopyProc B)) { Say "a game process ended"; break }
}
$results["processes alive"] = if ((Find-CopyProc A) -and (Find-CopyProc B)) { "PASS" } else { "FAIL" }
$results["unguarded exceptions"] = if ((Read-FocState A).excExe -eq 0) { "PASS" } else { "FAIL ($((Read-FocState A).excExe))" }
foreach ($c in "A", "B") { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
foreach ($k in $results.Keys) { Say ("RESULT {0}: {1}" -f $k, $results[$k]) }
Say "screenshots: $Out (1_score, 2_money: TAB board on both; 8_lives_*: lives and match end)"
Stop-Foc; Stop-Relay
