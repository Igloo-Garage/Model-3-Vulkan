# Opens the settings window (no ROM) for a few seconds, to smoke-test the UI.
param([int]$Seconds = 10)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Model3-Vulkan.exe"
$log = Join-Path $root "build-cmake\bin\Data\Supermodel.log"
if (Test-Path $log) { Remove-Item $log -Force }

$p = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 500
Write-Output ("exited during run: " + $p.HasExited + "  exit code: " + $p.ExitCode)
