# Small throwaway helper: set ini keys for an automated test run.
# Usage: powershell -NoProfile -File Scripts/set-test-ini.ps1 -Key CameraCutAngleDegrees -Value 15
param(
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string]$Value
)

$path = Join-Path (Split-Path -Parent $PSScriptRoot) "build-cmake\bin\Data\Config\Model3-Vulkan.ini"
$lines = Get-Content $path
$found = $false
$out = foreach ($line in $lines) {
    if ($line -match ("^" + [regex]::Escape($Key) + "\s*=")) {
        $found = $true
        "$Key = $Value"
    } else {
        $line
    }
}
if (-not $found) { $out = @($out) + "$Key = $Value" }
Set-Content -Path $path -Value $out
Write-Output ("$Key = $Value (" + ($out | Measure-Object).Count + " lines)")
