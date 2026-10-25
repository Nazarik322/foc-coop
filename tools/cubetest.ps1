# B5 rest (reflections): on network clients the level's StaticSceneCaptureCubeMapActors were destroyed at level start,
# so guests' robots had no ClosestReflectionCubeMap (flat, cooler, "no metallic"). L01 intro with 2 copies; `fulldump`
# on host and guest; counts mesh components with a reflection cube map.
# PASS: the guest has at least half as many meshes with a cube map as the host (host ~220 in the intro).
# -Twin: cubefix=0 on the guest (must FAIL: guest 0).
param([string]$Out = "E:\FoC-coop\sessions\cubetest", [switch]$Twin, [switch]$Keep)
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Out | Out-Null
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content (Join-Path $Out "run.txt") $l; $l }
function Set-IniKey($c, $k, $v) {
  $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"
  $lines = @(Get-Content $p -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch "^$k=" }); if (-not $lines.Count) { $lines = @("[foc_coop]") }
  $lines += "$k=$v"; Set-Content $p $lines -Encoding ascii
}
Set-IniKey B "cubefix" $(if ($Twin) { 0 } else { 1 })
try {
  powershell -ExecutionPolicy Bypass -Command "& '$PSScriptRoot\lobbytest.ps1' -Map L01_TheExodus_BASE_m -Out '$Out\lobby' -Keep" 2>&1 | Select-String "RESULT" | ForEach-Object { Say "lobbytest: $($_.Line)" }
  if (-not (Read-FocState B) -or (Read-FocState B).role -ne "client") { Say "RESULT: SKIP guest not in the mission"; return }
  Start-Sleep 50
  foreach ($c in "A", "B") { Remove-Item "$Out\fd_$c.txt" -ErrorAction SilentlyContinue; Send-Cmd $c "fulldump $Out\fd_$c.txt"; Start-Sleep 2 }
  Start-Sleep 15
  $n = @{}
  foreach ($c in "A", "B") { $n[$c] = @(Select-String "$Out\fd_$c.txt" -Pattern "ObjectProperty\.ClosestReflectionCubeMap = StaticSceneCapture" -ErrorAction SilentlyContinue).Count }
  Say "meshes with a reflection cube map: host $($n['A']), guest $($n['B'])"
  if ($n['A'] -lt 20) { Say "RESULT: SKIP host has too few ($($n['A']))" }
  elseif ($n['B'] * 2 -ge $n['A']) { Say "RESULT: PASS guest robots have reflection cube maps (B5)" }
  else { Say "RESULT: FAIL guest robots without reflection cube maps (B5): guest $($n['B']) vs host $($n['A'])" }
} finally {
  Set-IniKey B "cubefix" 1
  if (-not $Keep) { Stop-Foc; Stop-Relay }
}
