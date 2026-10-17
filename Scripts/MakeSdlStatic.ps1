# Converts the bundled SDL2 and SDL_net projects from DLLs to static libraries.
#
# Why: the shipped folder is supposed to be a single Supermodel.exe with everything else under
# Data\. Implicit DLL imports can only be resolved from the executable's own directory, the system
# directories, or PATH - never from a subfolder - so while SDL2.dll and SDL2_net.dll are separate
# modules they have to sit next to the .exe. Building them into the executable removes the last two
# files from the top level.
#
# Four things have to change together, and getting any one of them wrong produces a confusing error
# rather than a clear one:
#
#   ConfigurationType   a static library is what the linker consumes.
#   RuntimeLibrary      the DLLs were built /MD while Supermodel is /MT. Mixing them in one module
#                       is LNK2038 (mismatched _ITERATOR_DEBUG_LEVEL / RuntimeLibrary). SDL_net
#                       already set this; SDL relied on the default.
#   OmitDefaultLibName  SDL's DLL build passes /Zl so its objects name no CRT. That is right for a
#                       DLL, which imports the host CRT, and wrong for a static library, which is
#                       meant to pull in the same static CRT the rest of the executable uses.
#   DLL_EXPORT          selects __declspec(dllexport) in SDL's headers. Nothing should be exported
#                       from a static library, and leaving it set would export the whole SDL API
#                       from Supermodel.exe.
#
# The .rc files are dropped because a library cannot carry resources.

$root = Split-Path -Parent $PSScriptRoot
$utf8 = [System.Text.UTF8Encoding]::new($false)

$targets = @(
    @{ Path = Join-Path $root 'MSVC/SDL/SDL.vcxproj';     Name = 'SDL' },
    @{ Path = Join-Path $root 'MSVC/SDLnet/SDL_net.vcxproj'; Name = 'SDL_net' }
)

foreach ($target in $targets)
{
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.AddRange([System.IO.File]::ReadAllLines($target.Path, $utf8))

    $config = ''
    $changes = [ordered]@{ ConfigType = 0; Runtime = 0; OmitLib = 0; DllExport = 0; Resource = 0 }

    for ($i = 0; $i -lt $lines.Count; $i++)
    {
        $line = $lines[$i]

        # Track which configuration the settings below belong to, for the runtime library choice.
        # Parsed by hand rather than with a regex: a '$' followed by '(' inside the condition is
        # ambiguous for the .NET regex parser, and a silently non-matching pattern here means every
        # configuration quietly gets the Release runtime library.
        if ($line -match 'ItemDefinitionGroup Condition=')
        {
            $eq = $line.LastIndexOf('==')
            if ($eq -ge 0)
            {
                $rest = $line.Substring($eq + 2)
                $open = $rest.IndexOf("'")
                $close = if ($open -ge 0) { $rest.IndexOf("'", $open + 1) } else { -1 }
                if ($open -ge 0 -and $close -gt $open)
                {
                    $config = $rest.Substring($open + 1, $close - $open - 1)
                }
            }
            continue
        }

        if ($line -match '<ConfigurationType>DynamicLibrary</ConfigurationType>')
        {
            $lines[$i] = $line -replace 'DynamicLibrary', 'StaticLibrary'
            $changes.ConfigType++
            continue
        }

        if ($line -match '<RuntimeLibrary>')
        {
            $want = if ($config -like 'Debug*') { 'MultiThreadedDebug' } else { 'MultiThreaded' }
            if ($line -notmatch "<RuntimeLibrary>$want</RuntimeLibrary>")
            {
                $indent = $line.Substring(0, $line.Length - $line.TrimStart().Length)
                $lines[$i] = "$indent<RuntimeLibrary>$want</RuntimeLibrary>"
                $changes.Runtime++
            }
            continue
        }

        if ($line -match '<OmitDefaultLibName>true</OmitDefaultLibName>')
        {
            $indent = $line.Substring(0, $line.Length - $line.TrimStart().Length)
            $lines[$i] = "$indent<OmitDefaultLibName>false</OmitDefaultLibName>"
            $changes.OmitLib++
            continue
        }

        if ($line -match '<PreprocessorDefinitions>.*\bDLL_EXPORT;')
        {
            # Add the runtime library alongside it when the project never set one, so the static
            # library agrees with the executable about which CRT it uses.
            if ($target.Name -eq 'SDL')
            {
                $want = if ($config -like 'Debug*') { 'MultiThreadedDebug' } else { 'MultiThreaded' }
                $lines[$i] = $line
                $indent = $line.Substring(0, $line.Length - $line.TrimStart().Length)
                $lines.Insert($i + 1, "$indent<RuntimeLibrary>$want</RuntimeLibrary>")
                $changes.Runtime++
            }
            $lines[$i] = $lines[$i] -replace '\bDLL_EXPORT;', ''
            $changes.DllExport++
            continue
        }

        if ($line -match '<ResourceCompile Include=')
        {
            # Commented out rather than deleted so the file still records what used to be here.
            $lines[$i] = '<!-- Static library: no resource section. -->' + $line.Trim()
            $changes.Resource++
            continue
        }
    }

    [System.IO.File]::WriteAllLines($target.Path, $lines, $utf8)

    $summary = ($changes.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '
    Write-Output "$($target.Name): $summary"
}
