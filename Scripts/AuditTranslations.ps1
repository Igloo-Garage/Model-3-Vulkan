# Audits every Chinese interface string in Gui.cpp against the English translation table.
#
# Written because "is this one line translated?" is not answerable by looking at a screenshot: in
# Chinese mode a missing entry is invisible, because Tr() hands the key straight back and Chinese
# always looks correct. Only an audit can tell you what would show Chinese while English is
# selected.
#
# The scan works on the whole file rather than on call arguments. An earlier version inspected the
# first argument of each Ui*() wrapper and reported everything translated, which was wrong: option
# lists such as the mask types and the input systems are string arrays passed to UiItems(), and
# their elements are never arguments of a wrapper themselves. Scanning every literal run catches
# both shapes.
#
# Literal runs are joined the way the compiler joins adjacent literals, so a value split across
# several source lines reconstructs into the one key Tr() will see at run time. Runs containing no
# CJK are skipped: they need no translation.

$root = Split-Path -Parent $PSScriptRoot
$utf8 = [System.Text.UTF8Encoding]::new($false)

$gui = [System.IO.File]::ReadAllText((Join-Path $root 'Src/OSD/SDL/Gui.cpp'), $utf8)

# Overlay.cpp is scanned too. It carries its own Chinese literals (the panel title, the Apply/Cancel
# buttons, the hint line) and Tr() looks them up in the same table, so leaving it out would let it
# ship Chinese-only strings in English mode while the audit still reported everything translated.
$overlay = [System.IO.File]::ReadAllText((Join-Path $root 'Src/OSD/SDL/Overlay.cpp'), $utf8)
$gui = $gui + "`n" + $overlay

# StatsOsd.cpp is scanned as a guard rather than because it has anything to translate: its labels are
# deliberately language-neutral (Renderer, CPU, GPU, VRAM, FPS) and are not looked up at all. Scanning
# it means a Chinese literal added there later cannot slip through untranslated - which is exactly the
# mistake the file's own header comment warns against.
$stats = [System.IO.File]::ReadAllText((Join-Path $root 'Src/OSD/SDL/StatsOsd.cpp'), $utf8)
$gui = $gui + "`n" + $stats

$loc = [System.IO.File]::ReadAllText((Join-Path $root 'Src/OSD/SDL/Localization.cpp'), $utf8)

function Unescape([string]$raw)
{
    $s = $raw -replace '\\\\', [char]1
    $s = $s -replace '\\n', "`n"
    $s = $s -replace '\\t', "`t"
    $s = $s -replace '\\r', "`r"
    $s = $s -replace '\\"', '"'
    $s = $s -replace [char]1, '\'
    return $s
}

# The set of keys the table already provides.
#
# An entry's key may be built from several adjacent literals, exactly like a string passed to Tr():
# the theme tooltips are written that way. Joining them here matters, because a line-based scan sees
# only the first fragment and then reports a translated string as missing.
$table = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in [regex]::Matches($loc, '(?m)^\s*\{\s*("(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\s*,'))
{
    $keyRaw = ''
    foreach ($lit in [regex]::Matches($m.Groups[1].Value, '"(?:[^"\\]|\\.)*"'))
    {
        $keyRaw += $lit.Value.Substring(1, $lit.Value.Length - 2)
    }
    [void]$table.Add((Unescape $keyRaw))
}

# Walk the file collecting string literals and remembering whether the previous token was also a
# literal with nothing but whitespace between them.
$runs = [System.Collections.Generic.List[object]]::new()
$current = $null
$lastLiteralEnd = -1

$i = 0
$line = 1
while ($i -lt $gui.Length)
{
    $ch = $gui[$i]

    if ($ch -eq "`n") { $line++; $i++; continue }

    # Skip line and block comments so quoted text inside them is not mistaken for a literal.
    if ($ch -eq '/' -and $i + 1 -lt $gui.Length -and $gui[$i + 1] -eq '/')
    {
        while ($i -lt $gui.Length -and $gui[$i] -ne "`n") { $i++ }
        continue
    }
    if ($ch -eq '/' -and $i + 1 -lt $gui.Length -and $gui[$i + 1] -eq '*')
    {
        $i += 2
        while ($i + 1 -lt $gui.Length -and -not ($gui[$i] -eq '*' -and $gui[$i + 1] -eq '/'))
        {
            if ($gui[$i] -eq "`n") { $line++ }
            $i++
        }
        $i += 2
        continue
    }

    if ($ch -eq '"')
    {
        $startLine = $line
        $startIndex = $i
        $i++
        $sb = New-Object System.Text.StringBuilder
        while ($i -lt $gui.Length)
        {
            $c = $gui[$i]
            if ($c -eq '\') { [void]$sb.Append($gui[$i]); [void]$sb.Append($gui[$i + 1]); $i += 2; continue }
            if ($c -eq '"') { $i++; break }
            if ($c -eq "`n") { $line++ }
            [void]$sb.Append($c); $i++
        }

        # Adjacent to the previous literal? Only whitespace may sit in between.
        $gapStart = $lastLiteralEnd
        $adjacent = $false
        if ($gapStart -ge 0)
        {
            $onlySpace = $true
            for ($k = $gapStart; $k -lt $startIndex; $k++)
            {
                if (-not [char]::IsWhiteSpace($gui[$k])) { $onlySpace = $false; break }
            }
            $adjacent = $onlySpace
        }

        if ($adjacent -and $current -ne $null)
        {
            $current.Text += $sb.ToString()
        }
        else
        {
            # Start is kept so the second check can walk back from the literal to the call it belongs
            # to and see whether that call translates. Line alone is not enough for that.
            $current = [pscustomobject]@{ Text = $sb.ToString(); Line = $startLine; Start = $startIndex }
            [void]$runs.Add($current)
        }
        $lastLiteralEnd = $i
        continue
    }

    $i++
}

$hasCjk = [regex]'[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]'

$missing = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new()
$checked = 0

foreach ($run in $runs)
{
    if (-not $hasCjk.IsMatch($run.Text)) { continue }
    $key = Unescape $run.Text
    if (-not $seen.Add($key)) { continue }
    $checked++
    if (-not $table.Contains($key)) { [void]$missing.Add([pscustomobject]@{ Line = $run.Line; Text = $key }) }
}

# --- Second check: are the literals actually looked up? ------------------------------------------
#
# The first check alone cannot catch the bug it was written for. It answers "is there an English entry
# for this Chinese text?", and a string that is passed to ImGui as a bare literal - never wrapped in
# Tr() - has an entry and still shows Chinese with English selected. The table was complete and the
# label was still wrong, which is exactly the failure that shipped: the game-list filter hint had a
# "搜索游戏" literal and a matching entry, and the audit reported everything translated.
#
# So this check asks the other question: is each CJK literal actually handed to something that
# translates it? Either Tr(), or a helper that calls it internally. Anything else is a string that
# will render as-is whatever the language setting says.
#
# Which helpers translate is worked out rather than listed. A hand-written list was tried first and
# was wrong in both directions at once: it omitted several wrappers that do translate (reporting 121
# good lines) and it contained BoolSetting, which translates one level down in UiCheckbox - and a list
# that is wrong in the permissive direction hides the very bugs the check exists to find.
#
# So: parse each function body, treat it as translating if it calls Tr(), then repeatedly promote any
# function that calls an already-translating one until nothing changes. That handles however many
# layers of wrapper the interface grows, and cannot go stale.
$defs = @{}
# Matches a function signature followed by its opening brace, whether the brace is on the same line
# or the next. Requiring it on a line of its own was the first attempt and missed every one-line
# helper - UiText, UiButton, UiSep and the rest are all defined that way, and a missed definition
# makes the helper look like it does not translate.
$defRegex = [regex]'(?m)^[^\S\n]*(?:static\s+)?(?:void|bool|int|unsigned|float|double|std::string|const\s+char\s*\*|[A-Za-z_][\w:<>,\*&\s]*?)\b([A-Za-z_]\w*)\s*\([^{;]*\)\s*(?:\r?\n[^\S\n]*)?\{'
foreach ($m in $defRegex.Matches($gui))
{
    $name = $m.Groups[1].Value
    if ($defs.ContainsKey($name)) { continue }

    # Brace match from the opening brace to find the body.
    $open = $m.Index + $m.Length - 1
    $depth = 0
    $end = $open
    for ($k = $open; $k -lt $gui.Length; $k++)
    {
        if ($gui[$k] -eq '{') { $depth++ }
        elseif ($gui[$k] -eq '}')
        {
            $depth--
            if ($depth -eq 0) { $end = $k; break }
        }
    }
    $defs[$name] = $gui.Substring($open, $end - $open + 1)
}

$translating = [System.Collections.Generic.HashSet[string]]::new()
[void]$translating.Add('Tr')
$grew = $true
while ($grew)
{
    $grew = $false
    foreach ($name in @($defs.Keys))
    {
        if ($translating.Contains($name)) { continue }
        $body = $defs[$name]
        foreach ($known in @($translating))
        {
            if ($body -match ('(?<![\w:])' + [regex]::Escape($known) + '\s*\('))
            {
                [void]$translating.Add($name)
                $grew = $true
                break
            }
        }
    }
}

# Control-flow keywords are not calls: a literal inside `if (x == "...")` is a comparison, and the
# same key is looked up wherever the value is finally shown. Skipped rather than reported.
$notCalls = @('if', 'for', 'while', 'switch', 'return', 'case', 'else')

function GetCallName([string]$text, [int]$literalStart)
{
    # Walk back to the '(' that opens the call this literal belongs to, then read the identifier
    # before it. Depth tracking so that a literal inside a nested call is attributed to the innermost
    # one - which is the call that will actually receive it.
    $depth = 0
    for ($k = $literalStart - 1; $k -ge 0; $k--)
    {
        $c = $text[$k]
        if ($c -eq ')') { $depth++ }
        elseif ($c -eq '(')
        {
            if ($depth -gt 0) { $depth--; continue }
            $j = $k - 1
            while ($j -ge 0 -and ([char]::IsWhiteSpace($text[$j]))) { $j-- }
            $end = $j + 1
            while ($j -ge 0 -and ($text[$j] -match '[A-Za-z0-9_]')) { $j-- }
            return $text.Substring($j + 1, $end - $j - 1)
        }
    }
    return ''
}

$unwrapped = [System.Collections.Generic.List[object]]::new()
$seenUnwrapped = [System.Collections.Generic.HashSet[string]]::new()
foreach ($run in $runs)
{
    if (-not $hasCjk.IsMatch($run.Text)) { continue }
    $key = Unescape $run.Text
    $call = GetCallName $gui $run.Start
    # No enclosing call means the literal is data rather than an argument - an element of a label
    # table, say, which is looked up through Tr() at the point of use instead. Nothing to judge here,
    # and reporting these drowned the real finding in 121 lines of them the first time this ran.
    if ([string]::IsNullOrEmpty($call)) { continue }
    if ($notCalls -contains $call) { continue }
    if ($translating.Contains($call)) { continue }
    if (-not $seenUnwrapped.Add($key)) { continue }
    [void]$unwrapped.Add([pscustomobject]@{ Line = $run.Line; Call = $call; Text = $key })
}

Write-Output "Table keys                             : $($table.Count)"
Write-Output "Distinct Chinese strings in Gui.cpp     : $checked"
Write-Output "  (plus Overlay.cpp, scanned above too)"
Write-Output "Missing from the table              : $($missing.Count)"
Write-Output "Not passed through Tr()             : $($unwrapped.Count)"
Write-Output ""

if ($missing.Count -gt 0)
{
    Write-Output '--- shows Chinese even with English selected ---'
    foreach ($item in ($missing | Sort-Object Line))
    {
        $flat = ($item.Text -replace "`n", '\n') -replace "`r", ''
        if ($flat.Length -gt 76) { $flat = $flat.Substring(0, 76) + '...' }
        Write-Output ("  line {0,-5} {1}" -f $item.Line, $flat)
    }
}
else
{
    Write-Output 'All Chinese strings in Gui.cpp have an English entry.'
}

if ($unwrapped.Count -gt 0)
{
    Write-Output ""
    Write-Output '--- has an entry but is never looked up, so it cannot be translated ---'
    foreach ($item in ($unwrapped | Sort-Object Line))
    {
        $flat = ($item.Text -replace "`n", '\n') -replace "`r", ''
        if ($flat.Length -gt 60) { $flat = $flat.Substring(0, 60) + '...' }
        Write-Output ("  line {0,-5} via {1,-20} {2}" -f $item.Line, $item.Call, $flat)
    }
}
else
{
    Write-Output 'Every Chinese string in Gui.cpp is passed through Tr().'
}
