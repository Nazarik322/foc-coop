. "$PSScriptRoot\dev.ps1"
$pa = Find-CopyProc "A"; $hA = [W]::Find($pa.Id)
FocusWin $hA; [W]::Key(0x0D); Start-Sleep 12
Shot $hA "$env:TEMP\foc_matrix\host_after_pick.png" | Out-Null
foreach ($k in 0x11, 0x20, 0x1F) { HoldKey $hA $k 1500; Start-Sleep 1 }
Start-Sleep 3
Shot $hA "$env:TEMP\foc_matrix\host_after_move.png" | Out-Null
