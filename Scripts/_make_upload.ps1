$ErrorActionPreference = 'Stop'
$root = 'e:\Dev\Sega Model 3 Arcade Emulator\Supermodel-0.3a-20260928-git-8b4de23'
$dest = Join-Path $root 'UploadToGithub_20261017'

if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Path $dest | Out-Null

$dirs = @('Src','Docs','Config','Assets','MSVC','Makefiles','Scripts','.github','.vscode','GraphicsCompare')

$files = @(
  '.gitignore','build.bat','README.md','Supermodel.bat','Supermodel3Bezel.png'
)

foreach ($d in $dirs) {
    $src = Join-Path $root $d
    if (Test-Path $src) { Copy-Item $src -Destination (Join-Path $dest $d) -Recurse -Force }
}
foreach ($f in $files) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src -Destination (Join-Path $dest $f) -Force }
}

# Chinese-named working documents stay out: they are notes for this fork's author, not part of the
# emulator. Matched by pattern because a literal non-ASCII name is read in the console code page.

$libDst = Join-Path $dest 'Libraries'
New-Item -ItemType Directory -Path $libDst -Force | Out-Null
foreach ($f in @('DIR.txt','Get_Libs.cmd','Get_SDL.cmd','Get_SDL_Net.cmd','Get_Vulkan.cmd','Get_ZLib.cmd')) {
    $src = Join-Path $root ('Libraries\' + $f)
    if (Test-Path $src) { Copy-Item $src -Destination (Join-Path $libDst $f) -Force }
}

$imgSrc = Join-Path $root 'imgui-master'
if (Test-Path $imgSrc) {
    Copy-Item $imgSrc -Destination (Join-Path $dest 'imgui-master') -Recurse -Force
    Get-ChildItem (Join-Path $dest 'imgui-master') -Recurse -Directory -Force |
        Where-Object { $_.Name -eq '.git' } | ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
}

# Build output per sub-project.
foreach ($b in @('MSVC\x64','MSVC\SDL','MSVC\ZLib','MSVC\SDLmain','MSVC\SDLnet','MSVC\Musashi68K')) {
    $t = Join-Path $dest $b
    if (Test-Path $t) {
        Get-ChildItem $t -Directory -Force |
            Where-Object { $_.Name -in @('x64','x86','Debug','Release','DebugPublic') } |
            ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
    }
}

Get-ChildItem $dest -Recurse -File -Force -Include '*.obj','*.pdb','*.ilk','*.iobj','*.ipdb','*.exp','*.lib','*.res','*.tlog','*.lastbuildstate','*.FileListAbsolute.txt','*.exe','*.dll' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $dest -Recurse -Directory -Force -Filter '*.tlog' -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

Get-ChildItem (Join-Path $dest 'Src\Graphics\Vulkan3D\Shaders') -Filter '*.spv' -ErrorAction SilentlyContinue |
    Remove-Item -Force

foreach ($p in @('Src\CPU\68K\Musashi\m68kopac.c','Src\CPU\68K\Musashi\m68kopdm.c',
                 'Src\CPU\68K\Musashi\m68kopnz.c','Src\CPU\68K\Musashi\m68kops.c',
                 'Src\CPU\68K\Musashi\m68kops.h')) {
    $t = Join-Path $dest $p
    if (Test-Path $t) { Remove-Item $t -Force }
}

# Docs\Images: keep only what README.md actually embeds. Daytona2_2.gif and Scud.gif are not
# referenced by any document in the tree, and together they are 24 MB.
$imgDir = Join-Path $dest 'Docs\Images'
foreach ($n in @('Daytona2_2.gif','Scud.gif')) {
    $t = Join-Path $imgDir $n
    if (Test-Path $t) { Remove-Item $t -Force }
}

$count = (Get-ChildItem $dest -Recurse -File | Measure-Object).Count
$size = [math]::Round(((Get-ChildItem $dest -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
"FILES: $count"
"SIZE : $size MB"
