# Stage 6 test: injected keys move a pawn although NEITHER game window has focus (host A and guest B independently).
# Starts host + guest on L06 (matrix -Keep), moves the focus to another window (explorer tray / notepad), then
# `input hold W 2000` on the guest: its own pawn must move and the host's must not; then the same on the host.
# Twin: -Twin writes inputinject=0 into both inis; the result must be FAIL (nothing moves).
# -Mode <bits>: ini inputmode for experiments (1 DirectInput, 2 key messages, 4 GetAsyncKeyState/GetKeyState, 8 focus spoof).
# -Build: build + deploy this worktree first. Usage: powershell -ExecutionPolicy Bypass -Command "& tools\inputtest.ps1 -Build"
param([string]$Map = "L06_Megatron_BASE_m", [string]$Out = "", [switch]$Twin, [switch]$Build, [int]$Mode = 6, [int]$HoldMs = 2000, [string]$Key = "W",
      [string]$SlotOwner = "", [switch]$NoSlot, [string]$HostCopy = "A", [string]$GuestCopy = "B")
. "$PSScriptRoot\dev.ps1"
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class IFg {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static uint FgPid() { uint p; GetWindowThreadProcessId(GetForegroundWindow(), out p); return p; }
}
"@
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
if (-not $Out) { $Out = "E:\FoC-coop\sessions\input_$(Get-Date -Format yyyyMMdd_HHmm)_$Map" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $Out "input.txt"
function Say($s) { $l = "$(Get-Date -Format HH:mm:ss) $s"; Add-Content $log $l; $l }
$repo = Split-Path -Parent $PSScriptRoot
if ($Build) { if (-not (Build-Deploy -Repo $repo -NoSlot)) { Say "RESULT: FAIL build"; return } }
$inis = @{}
foreach ($c in @($HostCopy, $GuestCopy)) { $p = "E:\FoC-coop\$c\Binaries\foc_coop.ini"; $inis[$p] = Get-Content $p -Raw }
$extra = @("inputinject=$(if ($Twin) { 0 } else { 1 })", "inputmode=$Mode")
foreach ($p in $inis.Keys) { Set-Content $p ($inis[$p].TrimEnd() + "`r`n" + ($extra -join "`r`n") + "`r`n") -Encoding ascii }
function Pos($copy) { $s = Read-FocState $copy; $s.players | Where-Object { [bool]$_.local -and $_.pawn } | Select-Object -First 1 }
function Moved($a, $b) { if (-not $a -or -not $b) { return -1 }; [int][math]::Sqrt([math]::Pow($a.x - $b.x, 2) + [math]::Pow($a.y - $b.y, 2)) }
try {
  $m = & "$PSScriptRoot\matrix.ps1" -NoSlot -Maps $Map -Out $Out -AliveSec 5 -Keep | Select-String "RESULT" | ForEach-Object { $_.Line }
  Say "start: $m"
  if ($m -notlike "RESULT: PASS*") { Say "RESULT: SKIP $Map cannot start"; return }
  $pa = (Find-CopyProc $HostCopy).Id; $pb = (Find-CopyProc $GuestCopy).Id
  $sa = Read-FocState $HostCopy; $sb = Read-FocState $GuestCopy
  Say "mode: hostMap=$($sa.map) hostRole=$($sa.role) guestRole=$($sb.role) hostPlayers=$(@($sa.players).Count) guestPlayers=$(@($sb.players).Count) inputinject=$(if ($Twin) {0} else {1}) inputmode=$Mode"
  if ($sa.map -notlike "*$($Map -replace '_BASE_m','')*" -or $sa.role -ne "host" -or $sb.role -ne "client" -or @($sa.players).Count -ne 2) {
    Say "RESULT: FAIL $Map wrong mode (map/role/players)"; return }
  Start-Sleep 5
  # take the focus away from both game windows
  # a small form of our own takes the focus (no desktop screenshot involved)
  Add-Type -AssemblyName System.Windows.Forms
  $np = New-Object Windows.Forms.Form; $np.Text = "focus holder"; $np.Width = 240; $np.Height = 120; $np.TopMost = $true
  $np.Show(); $np.Activate(); [void][W]::Focus($np.Handle); [Windows.Forms.Application]::DoEvents(); Start-Sleep 1; [Windows.Forms.Application]::DoEvents()
  $fg = [IFg]::FgPid()
  Say "foreground pid $fg (host $pa guest $pb)"
  if ($fg -eq $pa -or $fg -eq $pb -or $fg -eq 0) { Say "RESULT: SKIP could not move the focus away from the game windows"; return }
  $fails = @(); $res = @()
  foreach ($who in @($GuestCopy, $HostCopy)) {
    $other = if ($who -eq $GuestCopy) { $HostCopy } else { $GuestCopy }
    $w0 = Pos $who; $o0 = Pos $other
    Send-Cmd $who "shot $Out\before_$who.bmp"; Start-Sleep 2
    Send-Cmd $who "input hold $Key $HoldMs"; Start-Sleep -Milliseconds ($HoldMs + 2500)
    $w1 = Pos $who; $o1 = Pos $other
    Send-Cmd $who "shot $Out\after_$who.bmp"; Start-Sleep 2
    $dw = Moved $w0 $w1; $do = Moved $o0 $o1
    $fg2 = [IFg]::FgPid()
    Say "hold $Key on $who : own pawn moved $dw uu, other ($other) moved $do uu, foreground pid $fg2"
    $res += "$who=$dw/$other=$do"
    if ($fg2 -eq $pa -or $fg2 -eq $pb) { $fails += "a game window got the focus during the $who hold" }
    if ($dw -lt 150) { $fails += "$who did not move ($dw uu)" }
    if ($do -gt 40) { $fails += "$other moved ($do uu) while only $who got keys" }
  }
  if (-not (Get-Process -Id $pa -ErrorAction SilentlyContinue)) { $fails += "host died" }
  if (-not (Get-Process -Id $pb -ErrorAction SilentlyContinue)) { $fails += "guest died" }
  foreach ($c in @($HostCopy, $GuestCopy)) { Copy-Item "E:\FoC-coop\$c\Binaries\foc_coop.log" (Join-Path $Out "$c.log") -ErrorAction SilentlyContinue }
  if ($fails) { Say "RESULT: FAIL $Map $($fails -join '; ') [$($res -join ' ')]" } else { Say "RESULT: PASS $Map unfocused injected $Key moved guest and host independently [$($res -join ' ')]" }
} finally {
  foreach ($p in $inis.Keys) { Set-Content $p $inis[$p] -NoNewline -Encoding ascii }
  if ($np) { $np.Close() }
  Stop-Foc; Stop-Relay
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
