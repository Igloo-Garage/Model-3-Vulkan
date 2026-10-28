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
    'real3d.vert', 'real3d.frag', 'real3d_oit.frag',
    'oit_resolve.vert', 'oit_resolve.frag',
    'scene_composite.vert', 'scene_composite.frag', 'scene_composite_ms.frag',
    'scene_composite.glsl',
    'real3d_common.glsl',
    'composite.vert', 'composite.frag',
    'texdecode.comp',
    'los.comp',
    'fog.vert', 'fog.frag',
    'post.frag',
    'crosshair.vert', 'crosshair.frag',
    'border.vert', 'border.frag'
)

$stale = @()

foreach ($name in $sources)
{
    $source = Join-Path $shaderDir $name
    # real3d_common.glsl is #included by the two scene shaders rather than compiled on its own,
    # so it has no header of its own; editing it has to invalidate both of their headers.
    $headerNames = if ($name -eq 'real3d_common.glsl')
        { @('real3d_frag.spv.h', 'real3d_oit_frag.spv.h') }
    elseif ($name -eq 'scene_composite.glsl')
        { @('scene_composite_frag.spv.h', 'scene_composite_ms_frag.spv.h') }
    else
        { @(($name -replace '\.', '_') + '.spv.h') }

    if (-not (Test-Path $source))
    {
        Write-Host "Shader check: $name is missing." -ForegroundColor Red
        exit 1
    }

    foreach ($headerName in $headerNames)
    {
        $header = Join-Path $shaderDir $headerName

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
