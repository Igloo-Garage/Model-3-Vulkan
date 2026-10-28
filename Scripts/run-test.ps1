# Runs the SDL3 binary against a ROM set for a fixed time, then stops it.
# Usage: powershell -NoProfile -File Scripts/run-test.ps1 -Rom E:/EMU/model-3-romset/harley.zip -Seconds 25
param(
    [string]$Rom = "E:/EMU/model-3-romset/harley.zip",
    [int]$Seconds = 25
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Model3-Vulkan.exe"
$log = Join-Path $root "build-cmake\bin\Data\Supermodel.log"
if (Test-Path $log) { Remove-Item $log -Force }

$p = Start-Process -FilePath $exe -ArgumentList $Rom -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 500
Write-Output ("exit code: " + $p.ExitCode)
