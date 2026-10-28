# Starts a game with the pad probe enabled, so a controller can be tested by hand.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\PadProbe.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\PadProbe.ps1 -Set daytona2
#
# Press the buttons/sticks/triggers you want to check, quit the game with Esc, then read the log:
#
#   Select-String Data\Supermodel.log -Pattern "Input:"
#
# Every pad is listed with its name and capabilities when opened, and each press appears as
# "Input: pad N <button> down/up" (both the raw button number and, for a pad SDL recognises, the
# standard name). Axes are reported in 0.25 steps. The pad is also rumbled once at open.
param(
    [string]$Set = "daytona2",
    [string]$Rom = ""
)

$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "dist\Supermodel-VK"
if (-not (Test-Path (Join-Path $bin "Model3-Vulkan.exe"))) {
    $bin = Join-Path $root "build-cmake\bin"
}

$exe = Join-Path $bin "Model3-Vulkan.exe"
if (-not (Test-Path $exe)) {
    Write-Output "Model3-Vulkan.exe not found in dist or build-cmake."
    exit 1
}

if ($Rom -eq "") {
    # Take ROMPath from the ini next to the executable, so a bare set name works regardless of
    # the shell's working directory.
    $ini = Join-Path $bin "Data\Config\Model3-Vulkan.ini"
    $romPath = "E:\EMU\model-3-romset"
    if (Test-Path $ini) {
        $line = Select-String -Path $ini -Pattern '^\s*ROMPath\s*=\s*(.*)$' | Select-Object -First 1
        if ($line -and $line.Matches[0].Groups[1].Value.Trim() -ne "") {
            $romPath = $line.Matches[0].Groups[1].Value.Trim()
        }
    }
    $Rom = Join-Path $romPath ($Set + ".zip")
}

if (-not (Test-Path $Rom)) {
    Write-Output "ROM not found: $Rom"
    exit 1
}

$env:SM3D_PAD_PROBE = "1"
Write-Output ("running {0}" -f $Rom)
Write-Output ("log: {0}" -f (Join-Path $bin "Data\Supermodel.log"))
Push-Location $bin
try {
    & $exe $Rom
} finally {
    Pop-Location
}
