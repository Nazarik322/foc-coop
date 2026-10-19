param([string]$Copy = "A", [string]$Keys = "0x0D", [string]$Name = "keys", [int]$Wait = 6)
. "$PSScriptRoot\dev.ps1"
$p = Find-CopyProc $Copy; $h = [W]::Find($p.Id)
FocusWin $h
foreach ($k in ($Keys -split ",")) { [W]::Key([byte][Convert]::ToInt32($k.Trim(), 16)); Start-Sleep -Milliseconds 700 }
Start-Sleep $Wait
Shot $h "$env:TEMP\foc_matrix\$Name.png" | Out-Null
