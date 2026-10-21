# Dev test helpers: dot-source this file. Drives a game copy like a player (real key/mouse events, screenshots).
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Drawing; using System.Drawing.Imaging;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint fl);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint fl, UIntPtr ex);
  [DllImport("user32.dll")] public static extern void mouse_event(uint fl, int dx, int dy, uint d, UIntPtr ex);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p != pid) return true; var c = new StringBuilder(128); GetClassName(h, c, 128); if (c.ToString() == "LaunchUnrealUWindowsClient") { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  public static bool Focus(IntPtr h) {
    for (int i = 0; i < 5; i++) {
      IntPtr fg = GetForegroundWindow(); uint pid;
      uint fgT = GetWindowThreadProcessId(fg, out pid), me = GetCurrentThreadId();
      keybd_event(0x12, 0, 0, UIntPtr.Zero);
      AttachThreadInput(me, fgT, true); BringWindowToTop(h); SetForegroundWindow(h); AttachThreadInput(me, fgT, false);
      keybd_event(0x12, 0, 2, UIntPtr.Zero);
      System.Threading.Thread.Sleep(300);
      if (GetForegroundWindow() == h) return true;
    }
    ShowWindow(h, 6); System.Threading.Thread.Sleep(500); ShowWindow(h, 9); System.Threading.Thread.Sleep(900);   // minimize/restore grants activation when the foreground lock refuses
    return GetForegroundWindow() == h;
  }
  public static string Shot(IntPtr h, string path) {
    RECT r; GetClientRect(h, out r); POINT p; p.X = 0; p.Y = 0; ClientToScreen(h, ref p);
    int w = r.R - r.L, hg = r.B - r.T;
    using (var bmp = new Bitmap(w, hg)) { using (var g = Graphics.FromImage(bmp)) { g.CopyFromScreen(p.X, p.Y, 0, 0, new Size(w, hg)); } bmp.Save(path, ImageFormat.Png); }
    return w + "x" + hg;
  }
  public static void Click(IntPtr h, int x, int y) {
    POINT p; p.X = x; p.Y = y; ClientToScreen(h, ref p); SetCursorPos(p.X, p.Y);
    System.Threading.Thread.Sleep(150);
    mouse_event(2, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(120); mouse_event(4, 0, 0, 0, UIntPtr.Zero);
  }
  public static void Move(IntPtr h, int x, int y) { POINT p; p.X = x; p.Y = y; ClientToScreen(h, ref p); SetCursorPos(p.X, p.Y); }
  public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(120); keybd_event(vk, 0, 2, UIntPtr.Zero); }
}
"@ -ReferencedAssemblies System.Drawing

# ---------------------------------------------------------------------------------------------------------------------
# Game slot lease (team rules, docs/dev/TEAM.md): the test copies, ports, foc_cmd.txt files, focus and the RDP display
# are one shared resource. Whoever runs games holds E:\FoC-coop\slot.lock (key=value lines: owner, pid, since, ttl).
#   Enter-FocSlot -Owner title            wait (poll every 10 s, up to -WaitMin) until the lease is free, then take it
#   Exit-FocSlot  -Owner title            give it back (only if owner and pid match; -Force removes any lease)
#   Get-FocSlot                           who holds it now ($null when free); Expired tells if the ttl ran out
#   Update-FocSlot                        renew our lease (long loops call this; resets "since")
# Owner: -Owner, else $env:FOC_LANE, else "manual". Re-entrant for the same owner and process id. An expired lease
# (since + ttl in the past, or an unreadable file older than 30 s) is taken over, and the takeover is logged.
# All lease functions print with Write-Host and return nothing on the pipeline (scripts parse their own output).
# ---------------------------------------------------------------------------------------------------------------------
function Get-FocSlotOwner([string]$Owner = "") {
  if ($Owner) { return $Owner }
  if ($env:FOC_LANE) { return $env:FOC_LANE }
  return "manual"
}
function Write-FocSlotLog([string]$LockPath, [string]$Msg) {
  Write-Host "slot: $Msg"
  try { Add-Content -LiteralPath "$LockPath.log" -Value ("{0} pid={1} {2}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $PID, $Msg) -ErrorAction Stop } catch { }
}
# Runs $Body while holding a machine-wide named mutex derived from the lock path (serialises check, takeover and create).
function Invoke-FocSlotGuard([string]$LockPath, [scriptblock]$Body) {
  $md5 = [Security.Cryptography.MD5]::Create()
  $hash = ([BitConverter]::ToString($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($LockPath.ToLowerInvariant()))) -replace '-', '')
  $mx = $null
  try { $mx = New-Object System.Threading.Mutex($false, "Global\FocSlot_$hash") } catch { try { $mx = New-Object System.Threading.Mutex($false, "Local\FocSlot_$hash") } catch { $mx = $null } }
  $got = $false
  try {
    if ($mx) {
      try { $got = $mx.WaitOne(20000) } catch [System.Threading.AbandonedMutexException] { $got = $true }
      if (-not $got) { throw "slot guard mutex busy for 20 s ($LockPath)" }
    }
    & $Body
  } finally {
    if ($mx) { if ($got) { try { $mx.ReleaseMutex() } catch { } }; $mx.Dispose() }
  }
}
function Get-FocSlot([string]$LockPath = "E:\FoC-coop\slot.lock") {
  if (-not (Test-Path -LiteralPath $LockPath)) { return $null }
  $text = $null; $age = 0
  try {
    $fs = [IO.File]::Open($LockPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try { $sr = New-Object IO.StreamReader($fs); $text = $sr.ReadToEnd() } finally { $fs.Dispose() }
    $age = ((Get-Date).ToUniversalTime() - (Get-Item -LiteralPath $LockPath).LastWriteTimeUtc).TotalSeconds
  } catch {
    # being created or removed right now: not parsable yet, not expired
    return [pscustomobject]@{ Path = $LockPath; Valid = $false; Owner = "?"; ProcId = 0; Since = $null; TtlMin = 0; ExpiresAt = $null; Expired = $false; Mine = $false }
  }
  $kv = @{}
  foreach ($line in ($text -split "`r?`n")) { $i = $line.IndexOf('='); if ($i -gt 0) { $kv[$line.Substring(0, $i).Trim()] = $line.Substring($i + 1).Trim() } }
  $ownerName = $kv["owner"]; $lockPid = 0; $ttl = 0; $since = [datetime]::MinValue
  $ok = [bool]$ownerName -and [int]::TryParse([string]$kv["pid"], [ref]$lockPid) -and [int]::TryParse([string]$kv["ttl"], [ref]$ttl) -and
        [datetime]::TryParse([string]$kv["since"], [Globalization.CultureInfo]::InvariantCulture, ([Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal), [ref]$since)
  if (-not $ok) {
    # empty or garbled file: a writer may be in the middle of it, so only an old one counts as expired
    return [pscustomobject]@{ Path = $LockPath; Valid = $false; Owner = "?"; ProcId = 0; Since = $null; TtlMin = 0; ExpiresAt = $null; Expired = ($age -gt 30); Mine = $false }
  }
  $exp = $since.AddMinutes($ttl)
  return [pscustomobject]@{ Path = $LockPath; Valid = $true; Owner = $ownerName; ProcId = $lockPid; Since = $since; TtlMin = $ttl; ExpiresAt = $exp
    Expired = ((Get-Date).ToUniversalTime() -gt $exp); Mine = ($lockPid -eq $PID) }
}
function Format-FocSlotText([string]$Owner, [int]$TtlMin) {
  return ("owner={0}`r`npid={1}`r`nsince={2}`r`nttl={3}`r`n" -f $Owner, $PID, [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"), $TtlMin)
}
function Enter-FocSlot {
  param([string]$Owner = "", [int]$TtlMin = 45, [int]$WaitMin = 60, [string]$LockPath = "E:\FoC-coop\slot.lock", [int]$PollSec = 10)
  $Owner = Get-FocSlotOwner $Owner
  $dir = Split-Path -Parent $LockPath; if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  $deadline = (Get-Date).AddMinutes($WaitMin); $lastNote = [datetime]::MinValue
  $attempt = {
    $cur = Get-FocSlot -LockPath $LockPath
    if ($cur) {
      if ($cur.Valid -and $cur.Owner -eq $Owner -and $cur.ProcId -eq $PID) {
        [IO.File]::WriteAllText($LockPath, (Format-FocSlotText $Owner $TtlMin)); return "reentrant"
      }
      if (-not $cur.Expired) { return "busy" }
      $what = if ($cur.Valid) { "owner=$($cur.Owner) pid=$($cur.ProcId) since=$($cur.Since.ToString('s'))Z ttl=$($cur.TtlMin) min" } else { "unreadable lock file" }
      Write-FocSlotLog $LockPath "$Owner takes over an expired lease ($what)"
      try { Remove-Item -LiteralPath $LockPath -Force -ErrorAction Stop } catch { return "busy" }
    }
    try { $fs = [IO.File]::Open($LockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None) } catch [IO.IOException] { return "busy" }
    try { $b = [Text.Encoding]::ASCII.GetBytes((Format-FocSlotText $Owner $TtlMin)); $fs.Write($b, 0, $b.Length) } finally { $fs.Dispose() }
    return "acquired"
  }
  while ($true) {
    $state = Invoke-FocSlotGuard $LockPath $attempt
    $state = @($state)[-1]
    if ($state -eq "acquired") { Write-FocSlotLog $LockPath "acquired by $Owner (ttl $TtlMin min)"; return }
    if ($state -eq "reentrant") { return }
    $cur = Get-FocSlot -LockPath $LockPath
    $heldBy = if ($cur -and $cur.Valid) { "$($cur.Owner) (pid $($cur.ProcId)) since $($cur.Since.ToString('HH:mm:ss'))Z, expires $($cur.ExpiresAt.ToString('HH:mm:ss'))Z" } elseif ($cur) { "unreadable lock file" } else { "unknown" }
    if ((Get-Date) -ge $deadline) {
      Write-FocSlotLog $LockPath "$Owner gave up after $WaitMin min, held by $heldBy"
      throw "slot busy: $LockPath is held by $heldBy; $Owner waited $WaitMin min. Retry later or ask the holder (Exit-FocSlot -Force removes a stale lease)."
    }
    if (((Get-Date) - $lastNote).TotalSeconds -ge 60) { Write-Host "slot: $Owner waiting, held by $heldBy"; $lastNote = Get-Date }
    Start-Sleep -Seconds $PollSec
  }
}
function Exit-FocSlot {
  param([string]$Owner = "", [string]$LockPath = "E:\FoC-coop\slot.lock", [switch]$Force, [switch]$PassThru)
  $Owner = Get-FocSlotOwner $Owner
  $body = {
    $cur = Get-FocSlot -LockPath $LockPath
    if (-not $cur) { return $false }
    if ($Force -or ($cur.Valid -and $cur.Owner -eq $Owner -and $cur.ProcId -eq $PID)) {
      try { Remove-Item -LiteralPath $LockPath -Force -ErrorAction Stop } catch { Write-FocSlotLog $LockPath "$Owner could not remove the lease: $($_.Exception.Message)"; return $false }
      Write-FocSlotLog $LockPath "released by $Owner$(if ($Force) { ' (forced)' })"; return $true
    }
    Write-FocSlotLog $LockPath "$Owner did not release: the lease belongs to $($cur.Owner) (pid $($cur.ProcId))"
    return $false
  }
  $r = @(Invoke-FocSlotGuard $LockPath $body)[-1]
  if ($PassThru) { return [bool]$r }
}
# Renews our own lease (same process id); silent when we hold none. Long loops call this so the ttl only matters after a crash.
function Update-FocSlot([string]$LockPath = "E:\FoC-coop\slot.lock") {
  $cur = Get-FocSlot -LockPath $LockPath
  if (-not $cur -or -not $cur.Valid -or $cur.ProcId -ne $PID) { return }
  $body = { $c = Get-FocSlot -LockPath $LockPath; if ($c -and $c.Valid -and $c.ProcId -eq $PID) { [IO.File]::WriteAllText($LockPath, (Format-FocSlotText $c.Owner $c.TtlMin)) } }
  Invoke-FocSlotGuard $LockPath $body | Out-Null
}
# For helpers that need the lease only for their own duration (Build-Deploy): take it unless this process already holds it.
# Returns $true when it was taken here (the caller must Exit-FocSlot), $false when it was already ours.
function Enter-FocSlotIfNeeded([string]$Owner = "", [int]$TtlMin = 45, [int]$WaitMin = 60, [string]$LockPath = "E:\FoC-coop\slot.lock") {
  $cur = Get-FocSlot -LockPath $LockPath
  if ($cur -and $cur.Valid -and $cur.ProcId -eq $PID -and -not $cur.Expired) { return $false }
  Enter-FocSlot -Owner $Owner -TtlMin $TtlMin -WaitMin $WaitMin -LockPath $LockPath
  return $true
}

function Start-Foc([string]$Copy,[string]$MapUrl = "", [int]$W = 960, [int]$H = 540, [string[]]$Extra = @()) {
  $bin = "E:\FoC-coop\$Copy\Binaries"
  $a = @(); if ($MapUrl) { $a += "`"$MapUrl`"" }; $a += @("-windowed", "ResX=$W", "ResY=$H", "-nohomedir") + @($Extra | Where-Object { $_ })
  # SteamAppId=480: the DLL would otherwise restart the game as Spacewar and this process id would be gone
  $env:SteamAppId = "480"; $env:SteamGameId = "480"
  $p = Start-Process -FilePath "$bin\TFOC.exe" -ArgumentList $a -WorkingDirectory $bin -PassThru
  $env:SteamAppId = $null; $env:SteamGameId = $null
  return $p.Id
}
function Wait-FocWindow([int]$ProcId, [int]$Timeout = 120) {
  for ($i = 0; $i -lt $Timeout; $i++) { $h = [W]::Find([uint32]$ProcId); if ($h -ne [IntPtr]::Zero) { Set-FocWindowSlot $ProcId $h; return $h }; Start-Sleep 1 }
  return [IntPtr]::Zero
}
# Owner 2026-10-20: the test windows stood on top of each other. Copy A (host) goes to the left, B to the right of it,
# C/D below (960x540 client area each); no resize, no focus change.
function Set-FocWindowSlot([int]$ProcId, $h) {
  try {
    $path = (Get-Process -Id $ProcId -ErrorAction Stop).Path
    if ($path -notmatch '\\FoC-coop\\([A-Z])\\') { return }
    $slot = [int][char]$Matches[1] - [int][char]'A'
    $x = ($slot % 2) * 980; $y = [math]::Floor($slot / 2) * 590
    [void][W]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 0x0015)   # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
  } catch {}
}
# B9: a window handle of 0 (the game has no window, e.g. the guest did not start) is noted, never an exception.
function Test-FocWin($h) { return ($null -ne $h -and $h -ne [IntPtr]::Zero) }
function Shot($h, [string]$out) {
  if (-not (Test-FocWin $h)) { Write-Host "note: no window, window screenshot skipped ($out)"; return }
  try { [W]::Shot($h, $out) } catch { Write-Host "note: window screenshot failed ($($_.Exception.Message)): $out" }
}
function FocusWin($h) {
  if (-not (Test-FocWin $h)) { Write-Host 'note: no window to focus'; return }
  $ok = [W]::Focus($h); if (-not $ok) { Write-Host 'WARNING: could not focus window' }; Start-Sleep -Milliseconds 300
}
function Send-Cmd([string]$Copy, [string]$Cmd) { Set-Content -Path "E:\FoC-coop\$Copy\Binaries\foc_cmd.txt" -Value $Cmd -NoNewline -Encoding ascii }
# Only test copies under E:\FoC-coop are stopped, never a game started from the Steam install.
# -Copies A,B: only processes under E:\FoC-coop\A\ and \B\ (no argument: every copy under E:\FoC-coop, as before).
function Test-FocCopyPath($path, [string[]]$Copies = @()) {
  if (-not $path) { return $false }
  if (-not $Copies -or $Copies.Count -eq 0) { return ($path -like "E:\FoC-coop\*") }
  foreach ($c in $Copies) { if ($path -like "E:\FoC-coop\$c\*") { return $true } }
  return $false
}
function Stop-Foc([string[]]$Copies = @()) { Get-Process TFOC -ErrorAction SilentlyContinue | Where-Object { Test-FocCopyPath $_.Path $Copies } | Stop-Process -Force }

# foc_state.json: written once per second by the DLL (gametick.inc) in each copy's Binaries folder.
function Read-FocState([string]$Copy) {
  $f = "E:\FoC-coop\$Copy\Binaries\foc_state.json"
  if (-not (Test-Path $f)) { return $null }
  try { return Get-Content $f -Raw -ErrorAction Stop | ConvertFrom-Json } catch { return $null }
}
function Clear-FocState([string]$Copy) { Remove-Item "E:\FoC-coop\$Copy\Binaries\foc_state.json" -ErrorAction SilentlyContinue }
# Poll the state of a copy until $Cond (a scriptblock with param($s)) is true; returns the state or $null on timeout.
function Wait-FocState([string]$Copy, [scriptblock]$Cond, [int]$Timeout = 90) {
  for ($i = 0; $i -lt $Timeout; $i++) { $s = Read-FocState $Copy; if ($s -and (& $Cond $s)) { return $s }; Start-Sleep 1 }
  return $null
}

# Repo to build from: -Repo, else $env:FOC_REPO, else the main checkout.
function Get-FocRepo([string]$Repo = "") {
  $r = if ($Repo) { $Repo } elseif ($env:FOC_REPO) { $env:FOC_REPO } else { "C:\GitRepo\foc-coop" }
  return $r.TrimEnd('\')
}
# foc_build.txt next to the deployed DLL: which checkout, branch and commit the DLL in a test copy was built from.
function Write-FocBuildInfo([string]$Repo, [string[]]$Copies) {
  $branch = "?"; $commit = "?"; $dirty = "?"
  try {
    $branch = ("" + (& git -C $Repo rev-parse --abbrev-ref HEAD)).Trim()
    $commit = ("" + (& git -C $Repo rev-parse HEAD)).Trim()
    $dirty = if (@(& git -C $Repo status --porcelain).Count -gt 0) { "1" } else { "0" }
  } catch { }
  $text = "repo=$Repo`r`nbranch=$branch`r`ncommit=$commit`r`ndirty=$dirty`r`ntime=$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ss')`r`n"
  foreach ($c in $Copies) { Set-Content -LiteralPath "E:\FoC-coop\$c\Binaries\foc_build.txt" -Value $text -NoNewline -Encoding ascii }
}
# Builds <Repo>\build.bat and puts dinput8.dll (+ relay) into the test copies. Takes the slot lease for its duration unless
# this process already holds it (-NoSlot: skip that). Stops only the games of -Copies.
function Build-Deploy([string]$Repo = "", [string[]]$Copies = @("A", "B"), [string]$SlotOwner = "", [switch]$NoSlot) {
  $Repo = Get-FocRepo $Repo
  if (-not (Test-Path -LiteralPath "$Repo\build.bat")) { Write-Host "Build-Deploy: no build.bat in $Repo"; return $false }
  $took = $false
  if (-not $NoSlot) { $took = Enter-FocSlotIfNeeded -Owner $SlotOwner }
  try {
    Stop-Foc -Copies $Copies; Stop-Relay -Copies $Copies; Start-Sleep 2
    $o = & cmd.exe /c "$Repo\build.bat" 2>&1
    $rc = $LASTEXITCODE
    $err = $o | Select-String "error C\d+|error LNK\d+|fatal error|[^a-z]error:"
    if ($rc -ne 0 -or $err) { Write-Host "Build-Deploy: build failed (exit $rc)"; $err | ForEach-Object { Write-Host $_.Line }; return $false }
    foreach ($c in $Copies) { Copy-Item "$Repo\build\dinput8.dll" "E:\FoC-coop\$c\Binaries\dinput8.dll" -Force }
    Write-FocBuildInfo $Repo $Copies
    Deploy-Relay -Repo $Repo -Copies $Copies -NoSlot
    return $true
  } finally {
    if ($took) { Exit-FocSlot -Owner $SlotOwner }
  }
}

# Steam relay into Binaries\foc_relay of the test copies. Test copies get the mock steam_api.dll (two players on one
# PC cannot both log into Steam); -Real puts the real 32-bit steam_api.dll (Steamworks.NET 2025.164.1) instead.
function Deploy-Relay([switch]$Real, [string[]]$Copies = @("A", "B"), [string]$Repo = "", [string]$SlotOwner = "", [switch]$NoSlot) {
  $Repo = Get-FocRepo $Repo
  $took = $false
  if (-not $NoSlot) { $took = Enter-FocSlotIfNeeded -Owner $SlotOwner }
  try {
    $o = & cmd.exe /c "$Repo\relay\build_relay.bat" 2>&1
    if ($o | Select-String " error") { $o | Select-String " error"; return }
    $api = if ($Real) { "C:\GitRepo\coop-anything\local\tools\steamworksnet\x\Windows-x86\steam_api.dll" } else { "$Repo\relay\build\mock\steam_api.dll" }
    foreach ($c in $Copies) {
      $d = "E:\FoC-coop\$c\Binaries\foc_relay"; New-Item -ItemType Directory -Force $d | Out-Null
      Copy-Item "$Repo\relay\build\FoCRelay.exe" $d -Force; Copy-Item $api "$d\steam_api.dll" -Force
    }
  } finally {
    if ($took) { Exit-FocSlot -Owner $SlotOwner }
  }
}
function Stop-Relay([string[]]$Copies = @()) { Get-Process FoCRelay -ErrorAction SilentlyContinue | Where-Object { Test-FocCopyPath $_.Path $Copies } | Stop-Process -Force }
function Read-RelayStatus([string]$Copy) {
  $f = "E:\FoC-coop\$Copy\Binaries\foc_relay\relay.status"; $h = @{}
  if (Test-Path $f) { Get-Content $f | ForEach-Object { $k, $v = $_ -split '=', 2; if (-not $h.ContainsKey($k)) { $h[$k] = $v } } }
  return $h
}

function Find-CopyProc([string]$Copy) {
  # newest TFOC process that belongs to the given test copy (the game restarts itself when hosting/joining)
  Get-Process TFOC -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "E:\FoC-coop\$Copy\*" } | Sort-Object StartTime | Select-Object -Last 1
}
function Wait-CopyWindow([string]$Copy, [int]$Timeout = 120, [int]$NotPid = 0) {
  for ($i = 0; $i -lt $Timeout; $i++) {
    $p = Find-CopyProc $Copy
    if ($p -and $p.Id -ne $NotPid) { $h = [W]::Find([uint32]$p.Id); if ($h -ne [IntPtr]::Zero) { return $h } }
    Start-Sleep 1
  }
  return [IntPtr]::Zero
}
function HoldKey($h, [byte]$sc, [int]$ms) { if (-not (Test-FocWin $h)) { Write-Host 'note: no window, key skipped'; return }; FocusWin $h; [W]::keybd_event(0,$sc,8,[UIntPtr]::Zero); Start-Sleep -Milliseconds $ms; [W]::keybd_event(0,$sc,10,[UIntPtr]::Zero) }
