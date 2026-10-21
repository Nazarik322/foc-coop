# Compares the gfxdiag files of host (A) and guest (B): Binaries\foc_gfx.txt (written every 30 s on story maps and on
# the `gfxdiag` command). Shows both players' position and view, the D3D9 counts of the last frame and every object
# class whose count differs, rendering classes (light, material, particle, level, texture, mesh, fog, post process,
# decal, shadow) first. Usage: gfxdiff.ps1 [-Host A] [-Guest B] [-Fresh] [-Out file]
#   -Fresh: send `gfxdiag` to both games first (they must be running) and wait for new files.
param([string]$HostCopy = "A", [string]$GuestCopy = "B", [switch]$Fresh, [string]$Out = "", [int]$Top = 60)
. "$PSScriptRoot\dev.ps1"
$fa = "E:\FoC-coop\$HostCopy\Binaries\foc_gfx.txt"; $fb = "E:\FoC-coop\$GuestCopy\Binaries\foc_gfx.txt"
if ($Fresh) {
  $ta = if (Test-Path $fa) { (Get-Item $fa).LastWriteTime } else { [datetime]::MinValue }
  $tb = if (Test-Path $fb) { (Get-Item $fb).LastWriteTime } else { [datetime]::MinValue }
  Send-Cmd $HostCopy "gfxdiag"; Start-Sleep -Milliseconds 400; Send-Cmd $GuestCopy "gfxdiag"
  for ($i = 0; $i -lt 20; $i++) { Start-Sleep 1; if ((Test-Path $fa) -and (Test-Path $fb) -and (Get-Item $fa).LastWriteTime -gt $ta -and (Get-Item $fb).LastWriteTime -gt $tb) { break } }
}
if (-not (Test-Path $fa) -or -not (Test-Path $fb)) { "gfxdiff: missing $fa or $fb"; return }
function Load($f) {
  $h = [ordered]@{ head = @(); classes = @{} }
  foreach ($l in Get-Content $f) {
    if ($l -match '^class (\S+) (\d+)$') { $h.classes[$Matches[1]] = [int]$Matches[2] } else { $h.head += $l }
  }
  $h
}
$a = Load $fa; $b = Load $fb
$lines = @()
$lines += "== host ($HostCopy)"; $lines += $a.head
$lines += "== guest ($GuestCopy)"; $lines += $b.head
$render = 'Light|Material|Particle|Emitter|Level|Texture|Mesh|Fog|PostProcess|Decal|Shadow|Lens|Sky|Environment|Reflection|Render|Shader|Matinee|Interp|Camera|Primitive|Fracture|Effect'
$rows = foreach ($k in ($a.classes.Keys + $b.classes.Keys | Sort-Object -Unique)) {
  $x = [int]$a.classes[$k]; $y = [int]$b.classes[$k]
  if ($x -ne $y) { [pscustomobject]@{ Class = $k; Host = $x; Guest = $y; Diff = $y - $x; Render = [bool]($k -match $render) } }
}
$lines += "== classes that differ (guest - host), rendering classes first"
$lines += ($rows | Sort-Object @{ e = { -not $_.Render } }, @{ e = { [math]::Abs($_.Diff) }; Descending = $true } | Select-Object -First $Top | Format-Table -AutoSize | Out-String).TrimEnd()
$lines += "== only on the host: $(@($rows | Where-Object { $_.Guest -eq 0 }).Count) classes; only on the guest: $(@($rows | Where-Object { $_.Host -eq 0 }).Count) classes"
if ($Out) { $lines | Set-Content $Out -Encoding utf8; "gfxdiff: written to $Out" } else { $lines }
