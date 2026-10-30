# Rewrites the expected SHA-256 of the startup artwork in
# Src/OSD/Windows/SplashResource.h.
#
# The splash picture is compiled into the executable as an RCDATA resource, so
# it is not a file a copy can be dropped over. This script records the hash of
# the artwork the build was made from; the program hashes the resource it finds
# at start-up and marks itself UNOFFICIAL when the two disagree, which is what a
# rebranded binary shows until someone patches the check itself.
#
# Run it after changing the artwork. build.bat and build-cmake.bat call it
# before compiling, so the normal path needs no manual step:
#
#     powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\UpdateSplashSignature.ps1
#
# The master image stays at the project root (with the space in its name) so
# there is one copy to edit; see the resource lines in MSVC/Model3VulkanResources.rc.
$root = Split-Path -Parent $PSScriptRoot
$artwork = Join-Path $root 'Igloo-Garage Presents.webp'
$header = Join-Path $root 'Src/OSD/Windows/SplashResource.h'

if (-not (Test-Path $artwork))
{
    Write-Output ("[splash] {0} is missing; the signature was left as it is." -f (Split-Path -Leaf $artwork))
    exit 0
}

if (-not (Test-Path $header))
{
    Write-Output "[splash] Src/OSD/Windows/SplashResource.h is missing."
    exit 1
}

# Get-FileHash is not available on every PowerShell this project is built with,
# so the hash is taken straight from .NET - the same API underneath either way.
$sha = [System.Security.Cryptography.SHA256]::Create()
try
{
    $hash = ($sha.ComputeHash([System.IO.File]::ReadAllBytes($artwork)) |
        ForEach-Object { $_.ToString('x2') }) -join ''
}
finally
{
    $sha.Dispose()
}

$text = [System.IO.File]::ReadAllText($header)
$pattern = '(?m)^#define\s+MODEL3VULKAN_SPLASH_SHA256\s+"[^"]*"'
if ($text -notmatch $pattern)
{
    Write-Output "[splash] MODEL3VULKAN_SPLASH_SHA256 was not found in SplashResource.h."
    exit 1
}

$updated = [System.Text.RegularExpressions.Regex]::Replace(
    $text, $pattern, ('#define MODEL3VULKAN_SPLASH_SHA256 "' + $hash + '"'))

if ($updated -ne $text)
{
    [System.IO.File]::WriteAllText($header, $updated)
    Write-Output ("[splash] signature updated to {0}" -f $hash)
}
else
{
    Write-Output ("[splash] signature unchanged ({0})" -f $hash)
}
