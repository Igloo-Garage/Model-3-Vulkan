# Runs the OpenGL-only binary for a fixed time, then stops it.
param(
    [string]$Rom = "E:/EMU/model-3-romset/harley.zip",
    [int]$Seconds = 20
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Supermodel-LegacyOG.exe"
$log = Join-Path $root "build-cmake\bin\Data\Supermodel.log"
if (Test-Path $log) { Remove-Item $log -Force }

$p = Start-Process -FilePath $exe -ArgumentList $Rom -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 500
Write-Output ("exit code: " + $p.ExitCode)
