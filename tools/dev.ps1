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

function Start-Foc([string]$Copy, [string]$MapUrl = "", [int]$W = 960, [int]$H = 540) {
  $bin = "E:\FoC-coop\$Copy\Binaries"
  $a = @(); if ($MapUrl) { $a += "`"$MapUrl`"" }; $a += @("-windowed", "ResX=$W", "ResY=$H", "-nohomedir")
  $p = Start-Process -FilePath "$bin\TFOC.exe" -ArgumentList $a -WorkingDirectory $bin -PassThru
  return $p.Id
}
function Wait-FocWindow([int]$ProcId, [int]$Timeout = 120) {
  for ($i = 0; $i -lt $Timeout; $i++) { $h = [W]::Find([uint32]$ProcId); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep 1 }
  return [IntPtr]::Zero
}
function Shot($h, [string]$out) { [W]::Shot($h, $out) }
function FocusWin($h) { $ok = [W]::Focus($h); if (-not $ok) { Write-Host 'WARNING: could not focus window' }; Start-Sleep -Milliseconds 300 }
function Send-Cmd([string]$Copy, [string]$Cmd) { Set-Content -Path "E:\FoC-coop\$Copy\Binaries\foc_cmd.txt" -Value $Cmd -NoNewline -Encoding ascii }
function Stop-Foc { Get-Process TFOC -ErrorAction SilentlyContinue | Stop-Process -Force }

function Build-Deploy {
  Stop-Foc; Start-Sleep 2
  $o = & cmd.exe /c "C:\GitRepo\foc-coop\build.bat" 2>&1
  $err = $o | Select-String "error"; if ($err) { $err; return $false }
  foreach ($c in "A","B") { Copy-Item C:\GitRepo\foc-coop\build\dinput8.dll "E:\FoC-coop\$c\Binaries\dinput8.dll" -Force }
  return $true
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
