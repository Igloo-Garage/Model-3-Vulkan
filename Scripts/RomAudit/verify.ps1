# Verifies RomAudit's verdicts by actually launching games and reading the emulator's own log.
#
# The reason this exists: RomAudit reasons about file sets, and reasoning about file sets is easy to
# get subtly wrong in ways that still look right. Three separate wrong answers were produced during
# its development - matching games by file name, treating a parent/child merge as a union, and asking
# whether a *set* was satisfiable when the real question was whether a *game* would run - and none of
# them was visible from the tool's own output.
#
# So the emulator is treated as the authority and the tool is checked against it. Whenever RomAudit is
# changed, run this on a handful of sets whose verdict changed and confirm the two agree.
#
# Usage:
#   verify.ps1                                  default sample
#   verify.ps1 -Sets "bassdx,ecap,scudplus"     specific sets
#
# Reads the title the emulator actually selected, which is how a mismatch shows up: when the tool says
# a set should not load and the emulator loads a neighbouring revision instead, the tool is wrong.
param(
    [string]$Sets = "scudplus,harley,vf3,magtruck,bassdx",
    [int]$WaitSeconds = 10,
    [string]$ExeDir = "",
    [string]$RomDir = ""
)

if (-not $ExeDir) {
    $ExeDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "dist\Model-3-Vulkan"
}
if (-not $RomDir) { $RomDir = "E:\EMU\model-3-romset-2020" }

if (-not (Test-Path (Join-Path $ExeDir "Supermodel-LegacyOG.exe"))) {
    Write-Error "Supermodel-LegacyOG.exe not found in $ExeDir - set -ExeDir"
    exit 2
}

$exe = Join-Path $ExeDir "Supermodel-LegacyOG.exe"
$log = Join-Path $ExeDir "Data\Supermodel.log"

foreach ($raw in ($Sets -split ",")) {
    $set = $raw.Trim()
    if (-not $set) { continue }

    $zip = Join-Path $RomDir "$set.zip"
    if (-not (Test-Path $zip)) {
        Write-Host ("{0,-12} : zip not present, skipped" -f $set)
        continue
    }

    # A stale log would be read as though it described this run.
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Get-Process -Name "Model3-Vulkan" -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Name "Supermodel-LegacyOG" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800

    $proc = Start-Process -FilePath $exe -ArgumentList "`"$zip`"" -WorkingDirectory $ExeDir -PassThru
    Start-Sleep -Seconds $WaitSeconds
    $alive = -not $proc.HasExited
    if ($alive) { Stop-Process -Id $proc.Id -Force }
    Start-Sleep -Milliseconds 600

    $lines = Get-Content $log -ErrorAction SilentlyContinue
    $titleHit = $lines | Select-String -Pattern "\[Info\]\s+Title:\s+(.+)$" | Select-Object -First 1
    $loaded = if ($titleHit) { $titleHit.Matches[0].Groups[1].Value.Trim() } else { "" }
    $errs = @($lines | Select-String -Pattern "\[Error\]")
    $extra = $lines | Select-String -Pattern "Extra Hardware:" | Select-Object -First 1

    Write-Host ("{0,-12} : running={1,-5} loaded-as='{2}'" -f $set, $alive, $loaded)
    if ($extra) { Write-Host ("                 " + $extra.Line.Trim()) }
    if ($errs.Count -gt 0) { $errs | Select-Object -First 3 | ForEach-Object { Write-Host ("                 ERR: " + $_.Line.Trim()) } }
}
