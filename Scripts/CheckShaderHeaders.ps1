# Fails the build if a shader source is newer than the SPIR-V header it is compiled into.
#
# The Vulkan shaders are precompiled: BuildShaders writes a .spv, and a .spv.h that embeds the words
# is checked in and #included by the C++ (see the #include "Shaders/post_frag.spv.h" lines). Nothing
# in the normal build notices when a .frag has been edited without the compile step being re-run, so
# the edit simply has no effect - the program keeps using the old compiled shader, and the symptom is
# a change that appears to do nothing rather than an error.
#
# That is exactly what happened with the post-processing shader: several rounds of edits went
# unnoticed because the embedded copy was hours older. This turns that silence into a build error.
#
# Run Scripts\CompileVulkanShaders.cmd to regenerate the headers and clear it.

$ErrorActionPreference = 'Stop'

$shaderDir = Join-Path $PSScriptRoot '..\Src\Graphics\Vulkan3D\Shaders'

$sources = @(
    'real3d.vert', 'real3d.frag',
    'composite.vert', 'composite.frag',
    'texdecode.comp',
    'fog.vert', 'fog.frag',
    'post.frag'
)

$stale = @()

foreach ($name in $sources)
{
    $source = Join-Path $shaderDir $name
    $header = Join-Path $shaderDir (($name -replace '\.', '_') + '.spv.h')

    if (-not (Test-Path $source))
    {
        Write-Host "Shader check: $name is missing." -ForegroundColor Red
        exit 1
    }

    if (-not (Test-Path $header))
    {
        $stale += "$name (no compiled header)"
        continue
    }

    if ((Get-Item $source).LastWriteTime -gt (Get-Item $header).LastWriteTime)
    {
        $stale += "$name (edited $((Get-Item $source).LastWriteTime.ToString('MM-dd HH:mm')), compiled $((Get-Item $header).LastWriteTime.ToString('MM-dd HH:mm')))"
    }
}

if ($stale.Count -gt 0)
{
    Write-Host ''
    Write-Host 'Shader sources are newer than the SPIR-V that gets embedded:' -ForegroundColor Yellow
    foreach ($item in $stale)
    {
        Write-Host "  $item" -ForegroundColor Yellow
    }
    Write-Host ''
    Write-Host 'The shaders are precompiled into .spv.h headers, so a source edit without recompiling' -ForegroundColor Yellow
    Write-Host 'has no effect at all - the old shader is what runs. Run this, then build again:' -ForegroundColor Yellow
    Write-Host ''
    Write-Host '    Scripts\CompileVulkanShaders.cmd' -ForegroundColor White
    Write-Host ''
    exit 1
}

Write-Host 'Shader check: headers are up to date.'
exit 0
