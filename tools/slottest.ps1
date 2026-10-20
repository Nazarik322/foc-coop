# Self-test of the slot lease in dev.ps1 (Enter-FocSlot / Exit-FocSlot / Get-FocSlot). Needs no game and uses a TEMP lock
# file, never E:\FoC-coop\slot.lock. Prints "RESULT: PASS/FAIL <case> <detail>" per case and "SUMMARY: pass=N fail=M".
#   a  acquire + release (and re-entrant for the same owner and process)
#   b  a second PowerShell process waits while the lease is held and gets it after the first released it
#   c  an expired lease is taken over (and logged); a corrupt old file too
#   d  Exit-FocSlot by a non-owner (other name, or same name with another pid) does not remove the lock; -Force does
#   e  a held, unexpired lease makes Enter-FocSlot fail clearly after -WaitMin
param([string]$Dir = "$env:TEMP\foc_slottest")
. "$PSScriptRoot\dev.ps1"
$lock = Join-Path $Dir "slot.lock"
if ($lock -ieq "E:\FoC-coop\slot.lock") { "refusing to test on the real lock file"; exit 2 }
if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
New-Item -ItemType Directory -Force $Dir | Out-Null
$pass = 0; $fail = 0
function Check([string]$name, [bool]$ok, [string]$detail) {
  if ($ok) { $script:pass++; "RESULT: PASS $name $detail" } else { $script:fail++; "RESULT: FAIL $name $detail" }
}
function Quiet([scriptblock]$b) { & $b 6>&1 | Out-Null }   # lease functions talk with Write-Host; keep the test output readable
function Write-Lock([string]$owner, [int]$lockPid, [datetime]$since, [int]$ttl) {
  Set-Content -LiteralPath $lock -Value ("owner={0}`r`npid={1}`r`nsince={2}`r`nttl={3}`r`n" -f $owner, $lockPid, $since.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ"), $ttl) -NoNewline -Encoding ascii
}

# a
Quiet { Enter-FocSlot -Owner t1 -LockPath $lock -WaitMin 1 -PollSec 1 }
$s = Get-FocSlot -LockPath $lock
$okA = ($s -and $s.Owner -eq "t1" -and $s.ProcId -eq $PID -and $s.TtlMin -eq 45 -and -not $s.Expired -and $s.Mine)
Quiet { Enter-FocSlot -Owner t1 -LockPath $lock -WaitMin 0 -PollSec 1 }   # re-entrant: no wait, no error
$s2 = Get-FocSlot -LockPath $lock
$okRe = ($s2 -and $s2.Owner -eq "t1" -and $s2.ProcId -eq $PID)
$rel = $null; Quiet { $script:rel = Exit-FocSlot -Owner t1 -LockPath $lock -PassThru }
Check "a acquire+release" ($okA -and $okRe -and $rel -eq $true -and -not (Test-Path $lock)) "owner=$($s.Owner) pid=$($s.ProcId) reentrant=$okRe released=$rel lockGone=$(-not (Test-Path $lock))"

# b
$child = Join-Path $Dir "child.ps1"
Set-Content -LiteralPath $child -Encoding ascii -Value @'
param($Dev, $Lock, $Owner)
. $Dev
Enter-FocSlot -Owner $Owner -LockPath $Lock -WaitMin 2 -PollSec 1
"ACQUIRED $PID"
Start-Sleep 4
Exit-FocSlot -Owner $Owner -LockPath $Lock
"RELEASED"
'@
Quiet { Enter-FocSlot -Owner t1 -LockPath $lock -WaitMin 1 -PollSec 1 }
$outFile = Join-Path $Dir "child.out"
$cp = Start-Process -FilePath powershell.exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $outFile -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $child, "-Dev", "$PSScriptRoot\dev.ps1", "-Lock", $lock, "-Owner", "t2")
Start-Sleep 12   # the child needs a few seconds to load dev.ps1; it must not have the lease while t1 holds it
$waiting = (-not $cp.HasExited) -and (-not ((Get-Content $outFile -ErrorAction SilentlyContinue) -match "ACQUIRED"))
$held = Get-FocSlot -LockPath $lock; $stillT1 = ($held -and $held.Owner -eq "t1")
Quiet { Exit-FocSlot -Owner t1 -LockPath $lock }
$got = $false; $owner2 = ""; $pid2 = 0
for ($i = 0; $i -lt 40 -and -not $got; $i++) {
  Start-Sleep -Milliseconds 500
  $h = Get-FocSlot -LockPath $lock
  if ($h -and $h.Owner -eq "t2") { $got = $true; $owner2 = $h.Owner; $pid2 = $h.ProcId }
}
$null = $cp.WaitForExit(30000)
$out = (Get-Content $outFile -ErrorAction SilentlyContinue) -join " "
Check "b second process waits, then acquires" ($waiting -and $stillT1 -and $got -and $pid2 -eq $cp.Id -and $out -match "RELEASED" -and -not (Test-Path $lock)) "waitedWhileHeld=$waiting t1KeptIt=$stillT1 acquiredAfterRelease=$got childPid=$($cp.Id) lockPid=$pid2 childReleased=$($out -match 'RELEASED')"

# c
Write-Lock "dead" 99999 ((Get-Date).AddHours(-2)) 1
$before = Get-FocSlot -LockPath $lock
Quiet { Enter-FocSlot -Owner t3 -LockPath $lock -WaitMin 0 -PollSec 1 }
$s = Get-FocSlot -LockPath $lock
$log = (Get-Content "$lock.log" -ErrorAction SilentlyContinue) -join "`n"
$okC = ($before.Expired -and $s.Owner -eq "t3" -and $s.ProcId -eq $PID -and $log -match "takes over an expired lease")
Quiet { Exit-FocSlot -Owner t3 -LockPath $lock }
Set-Content -LiteralPath $lock -Value "garbage" -NoNewline -Encoding ascii
(Get-Item $lock).LastWriteTime = (Get-Date).AddMinutes(-5)
Quiet { Enter-FocSlot -Owner t3 -LockPath $lock -WaitMin 0 -PollSec 1 }
$s = Get-FocSlot -LockPath $lock; $okCorrupt = ($s -and $s.Owner -eq "t3")
Quiet { Exit-FocSlot -Owner t3 -LockPath $lock }
Check "c expired lease taken over" ($okC -and $okCorrupt -and -not (Test-Path $lock)) "expiredTaken=$okC corruptOldTaken=$okCorrupt logged=$($log -match 'takes over')"

# d
Quiet { Enter-FocSlot -Owner t4 -LockPath $lock -WaitMin 1 -PollSec 1 }
$r1 = $null; Quiet { $script:r1 = Exit-FocSlot -Owner someoneelse -LockPath $lock -PassThru }
$still1 = (Get-FocSlot -LockPath $lock).Owner -eq "t4"
Quiet { Exit-FocSlot -Owner t4 -LockPath $lock }
Write-Lock "t4" 99999 (Get-Date) 45   # same owner name, other process id: not ours
$r2 = $null; Quiet { $script:r2 = Exit-FocSlot -Owner t4 -LockPath $lock -PassThru }
$still2 = (Test-Path $lock)
$r3 = $null; Quiet { $script:r3 = Exit-FocSlot -Owner t4 -LockPath $lock -Force -PassThru }
Check "d non-owner exit keeps the lock" ($r1 -eq $false -and $still1 -and $r2 -eq $false -and $still2 -and $r3 -eq $true -and -not (Test-Path $lock)) "otherName: removed=$r1 kept=$still1 | sameNameOtherPid: removed=$r2 kept=$still2 | force removed=$r3"

# e
Write-Lock "holder" 99999 (Get-Date) 45
$err = ""; $t0 = Get-Date
try { Quiet { Enter-FocSlot -Owner t5 -LockPath $lock -WaitMin 0 -PollSec 1 } } catch { $err = $_.Exception.Message }
$still = (Get-FocSlot -LockPath $lock).Owner -eq "holder"
Check "e busy lease fails clearly" ($err -match "slot busy" -and $err -match "holder" -and $still) "message='$err'"
Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue

Remove-Item -Recurse -Force $Dir -ErrorAction SilentlyContinue
"SUMMARY: pass=$pass fail=$fail"
if ($fail) { exit 1 }
