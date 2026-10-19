param([string]$Copy = "A", [string]$Name = "shot")
. "$PSScriptRoot\dev.ps1"
$p = Find-CopyProc $Copy; $h = [W]::Find($p.Id)
FocusWin $h; Shot $h "$env:TEMP\foc_matrix\$Name.png" | Out-Null
