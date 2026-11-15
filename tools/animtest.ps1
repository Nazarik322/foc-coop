# Ratchet T-pose on guests (L01 start): a scene robot's AnimNodeSequence keeps its name but loses / never gets its
# AnimSequence on the guest. animrebind (scene.inc) calls SetAnim again. The test clears the sequences of Ratchet's scene
# actor on the guest (`animunbind`) and checks with `animbind` that they are bound again within a few seconds.
# PASS: after 6 s Ratchet's L_01_WakingUp node has its AnimSequence again. -Twin: animrebind=0 on the guest (must FAIL).
param([string]$Out = "E:\FoC-coop\sessions\animtest", [switch]$Twin, [switch]$Keep, [string]$Actor = "TnMeshSetActorMAT_15804")
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" }); if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
function Bound($f) { for ($i = 0; $i -lt 20 -and -not (Test-Path $f); $i++) { Start-Sleep -Milliseconds 500 }; if (-not (Test-Path $f)) { return "missing" }; $l = @(Select-String $f -Pattern "^NODE $Actor seq=L_01_WakingUp" -ErrorAction SilentlyContinue | Select-Object -First 1); if (-not $l.Count) { return "missing" }; if ($l[0].Line -match "obj=- ") { "unbound" } else { "bound" } }
Set-IniKey B "animrebind" $(if ($Twin) { 0 } else { 1 })
try {
  powershell -ExecutionPolicy Bypass -Command "& '$PSScriptRoot\lobbytest.ps1' -Map L01_TheExodus_BASE_m -Out '$Out\lobby' -Keep" 2>&1 | Select-String "RESULT" | ForEach-Object { Say "lobbytest: $($_.Line)" }
  if (-not (Read-FocState B) -or (Read-FocState B).role -ne "client") { Say "RESULT: SKIP guest not in the mission"; return }
  Start-Sleep 70
  Send-Cmd B "animbind MeshSetActor $Out\before.txt"; Start-Sleep 4
  Send-Cmd B "animunbind $Actor"; Start-Sleep 3
  Send-Cmd B "animbind MeshSetActor $Out\cleared.txt"; Start-Sleep 6
  Send-Cmd B "animbind MeshSetActor $Out\after.txt"; Start-Sleep 4
  $b = Bound "$Out\before.txt"; $c = Bound "$Out\cleared.txt"; $a = Bound "$Out\after.txt"
  Say "Ratchet node: before $b, after animunbind $c, 6 s later $a"
  if ($b -eq "missing") { Say "RESULT: SKIP Ratchet's scene actor not found" }
  elseif ($a -eq "bound") { Say "RESULT: PASS Ratchet's animation bound again (animrebind)" }
  else { Say "RESULT: FAIL Ratchet's animation stays unbound (T-pose)" }
} finally {
  Set-IniKey B "animrebind" 1
  if (-not $Keep) { Stop-Foc; Stop-Relay }
}
