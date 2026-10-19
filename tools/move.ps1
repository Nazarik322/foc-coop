# Movement check: host (A) and guest (B) are already in a match (e.g. after matrix.ps1 left them running); hold W / D in each window
. "$PSScriptRoot\dev.ps1"
$pa = Find-CopyProc "A"; $pb = Find-CopyProc "B"
$hA = [W]::Find($pa.Id); $hB = [W]::Find($pb.Id)
foreach ($k in 0x11, 0x20, 0x1F) { HoldKey $hA $k 1500; Start-Sleep 1 }
Start-Sleep 2
foreach ($k in 0x11, 0x20, 0x1F) { HoldKey $hB $k 1500; Start-Sleep 1 }
