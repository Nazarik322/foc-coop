# Play bot over many chapters; one summary line per chapter in $Out\summary.txt.
param([string[]]$Maps = @("L01_TheExodus_BASE_m", "L02a_DefendTheArk_BASE_m", "L02b_DestroyAllMortars_BASE_m", "L03a_EyeOfTheStorm_BASE_m",
  "L03b_CutAndRun_BASE_m", "L04a_BridgeAssault_BASE_m", "L04b_StopTheTransport_BASE_m", "L04c_FlyingFortress_BASE_m", "L05_Soundwave_BASE_m",
  "L06_Megatron_BASE_m", "L08a_Starscream_BASE_m", "L08b_GrimlockSmash_BASE_m", "L09_FinalFight_BASE_m"), [int]$Minutes = 3,
  [string]$Out = "E:\FoC-coop\sessions\botall_$(Get-Date -Format yyyyMMdd_HHmm)", [string]$SlotOwner = "", [switch]$NoSlot)
. "$PSScriptRoot\dev.ps1"
$slotOwnerName = Get-FocSlotOwner $SlotOwner
if (-not $NoSlot) { Enter-FocSlot -Owner $slotOwnerName }
try {
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($m in $Maps) {
  $o = Join-Path $Out $m
  & "$PSScriptRoot\playbot.ps1" -Map $m -Minutes $Minutes -Out $o -NoSlot | Out-Null
  $res = (Get-Content (Join-Path $o "bot.txt") -ErrorAction SilentlyContinue | Select-String "BOT RESULT|BOT: cannot start" | Select-Object -Last 1).Line
  $probs = (Get-Content (Join-Path $o "bot.txt") -ErrorAction SilentlyContinue | Select-String "^\S+   - " | Select-Object -First 4 | ForEach-Object { $_.Line.Substring(12) }) -join " ; "
  $line = "$m :: $res :: $probs"
  Add-Content (Join-Path $Out "summary.txt") $line
  $line
}
} finally { if (-not $NoSlot) { Exit-FocSlot -Owner $slotOwnerName } }
