# End-to-end: host Escalation Ark from the in-game menu on copy A, join it from copy B through the lobby list.
param([string]$Shots = "$env:TEMP\foc_shots")
. "$PSScriptRoot\dev.ps1"
New-Item -ItemType Directory -Force $Shots | Out-Null
if (-not (Build-Deploy)) { return }
$pidA = Start-Foc "A" "" 960 540; $hA = Wait-FocWindow $pidA 90
Start-Sleep 5; [W]::Click($hA, 6, 6)
Start-Sleep 50
[W]::Key(0x79); Start-Sleep 1.5
[W]::Click($hA, 1280, 589); Start-Sleep 1          # Host a game
[W]::Click($hA, 1103, 450); Start-Sleep 1          # Escalation
Shot $hA "$Shots\host_page.png" | Out-Null
[W]::Click($hA, 878, 987)                          # Start hosting -> the game restarts into the session
Start-Sleep 8
$hA = Wait-CopyWindow "A" 120 $pidA
Start-Sleep 45
Shot $hA "$Shots\host_in_session.png" | Out-Null
$pidB = Start-Foc "B" "" 960 540; $hB = Wait-FocWindow $pidB 90
Start-Sleep 5; [W]::Click($hB, 6, 6)
Start-Sleep 50
[W]::Key(0x79); Start-Sleep 1.5
[W]::Click($hB, 1280, 660); Start-Sleep 4          # Join a game
Shot $hB "$Shots\client_lobbies.png" | Out-Null
[W]::Click($hB, 813, 582); Start-Sleep 1           # first lobby row
[W]::Click($hB, 878, 917)                          # Join -> the game restarts and connects
Start-Sleep 8
$hB = Wait-CopyWindow "B" 120 $pidB
Start-Sleep 50
Shot $hB "$Shots\client_after_join.png" | Out-Null
FocusWin $hA; Shot $hA "$Shots\host_after_join.png" | Out-Null
"done"
