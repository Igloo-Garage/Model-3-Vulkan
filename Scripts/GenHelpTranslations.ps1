# Generates the English translation entries for the (?) tooltips and the UiTooltip() blocks.
#
# The key has to be byte-identical to the string the code passes to Tr(), so the keys are lifted
# straight out of Gui.cpp rather than retyped: the argument of each HelpMarker()/UiTooltip() call is
# balanced-paren extracted, and the concatenated string literals inside it are joined the way the
# compiler would join them. Retping them by hand is how a key silently stops matching.

$root = Split-Path -Parent $PSScriptRoot
$guiPath = Join-Path $root 'Src/OSD/SDL/Gui.cpp'
$locPath = Join-Path $root 'Src/OSD/SDL/Localization.cpp'

$utf8 = [System.Text.UTF8Encoding]::new($false)
$gui = [System.IO.File]::ReadAllText($guiPath, $utf8)

function Extract-Arg([string]$s, [int]$open)
{
    $depth = 0
    for ($i = $open; $i -lt $s.Length; $i++)
    {
        $ch = $s[$i]
        if ($ch -eq '(') { $depth++ }
        elseif ($ch -eq ')')
        {
            $depth--
            if ($depth -eq 0) { return $s.Substring($open + 1, $i - $open - 1) }
        }
    }
    return ''
}

# Returns the runtime string for every multi-line call of $name, in source order.
function Get-HelpKeys([string]$source, [string]$name)
{
    $keys = @()
    foreach ($m in [regex]::Matches($source, "$name\s*\("))
    {
        $arg = Extract-Arg $source ($m.Index + $m.Length - 1)
        if ($arg -notmatch '\\n') { continue }      # single-line labels are handled elsewhere
        $joined = ([regex]::Matches($arg, '"((?:[^"\\]|\\.)*)"') | ForEach-Object { $_.Groups[1].Value }) -join ''
        $keys += $joined
    }
    return $keys
}

$keys = @()
$keys += Get-HelpKeys $gui 'HelpMarker'
$keys += Get-HelpKeys $gui 'UiTooltip'

# English text, in the same order. \n stays literal here so it is emitted as a C++ escape and
# becomes a real newline at run time. Single quotes are used in the prose so no \" escaping is
# needed. %s placeholders and the \\ in the script path are deliberate.
$english = @(
    # --- HelpMarker (advanced settings) ---------------------------------------------------------
    'On: one button - Y on a pad, D on the keyboard - cycles the four views, so a single key covers\n          all four. Each press steps to the next view and wraps around.\nOff: back to four separate view keys (B / Y / L3 / R3).\n\nOnly affects the games with four views (Daytona 2, Scud Race, Le Mans 24).'
    'The GPU multisamples polygon edges during rasterisation. Edges get noticeably smoother while\n    textures stay exactly as sharp - decals and small signage on the car body are not softened\n    the way a post-process antialiaser softens them.\n\nModel 3 polygon edges are very hard, so this gives the most benefit; prefer it on a discrete GPU.'
    'One extra full-screen pass that finds edges from the luma gradient and smooths them. Very cheap\nand runs on any GPU, but the whole picture softens slightly, texture detail included.\n\nGood for integrated graphics and thin laptops. With a discrete GPU prefer the hardware MSAA\nabove, or turn both on to cover each other''s gaps.'
    'Scales the picture by whole numbers only and leaves black borders where it does not fill.\n\nThe 2D tile layer (menus, text, sprites) is fixed at 496x384 in hardware; however high the 3D\nresolution, it is stretched over the top. With a fractional scale some source pixels get 5\nscreen pixels and others 6, which makes interface text look uneven; integer scaling gives every\nsource pixel an exact n x n block.\n\nAt 4K that is 5x: a visible area of 2480x1920. Because the panel is 16:9 and the game is 4:3,\nabout 680 pixels of border remain on each side and 120 above and below - narrower than with it off.\nAt 1080p it is 2x (992x768).\n\nThe cost is some screen area, so it is off by default. Turn it on for pixel-exact output.'
    'Simulates the scanlines of an arcade CRT and its phosphor mask (shadow mask).\n\nThe mask needs enough physical resolution to read as structure: at 4K it looks most like the\nreal thing, while at 1080p use scanlines alone or it tends to turn into moire. The filter\nmultiplies, so the side borders are not tinted.'
    'Pairs the scanline count with a mask pitch so the two do not interfere with each other.\n\nBoth are periodic patterns laid out in output pixels, and they beat whenever their periods are\nnot in a whole-number ratio, producing colour bands. The classic case is 540 lines: a 4-pixel\nperiod at 4K against a mask pitch of 3 (a slot mask really repeats every 6 pixels vertically)\ngives a 12-pixel rainbow, about 2.2 mm per band on a 32 inch panel.\n\nThe important part: moving either slider on its own is how you get there.\n\nThe scanline count in a preset is derived from the current output resolution rather than stored,\nbecause the periods are fixed in output pixels - so the look carries across resolutions (360\nlines at 4K is the same appearance as 180 at 1080p).\n\nThe mask pitch can only be a multiple of 3 (3, 6, 9), otherwise the three phosphor sub-holes\nfall inside less than one pixel and the mask itself becomes colour noise. Add the requirement\nthat the vertical period divide evenly and only the combinations below qualify. Every preset\nuses the slot mask (the pattern in Sega''s arcade cabinets).\n\nOnce anything below is edited by hand this reads Custom (manual) and states outright whether\nthe two periods are in a whole-number ratio.'
    'How many scanlines are laid out across the picture height. 0 = use the Model 3''s own 384 rows,\nso one scanline lines up with one original row, which is closest to the arcade cabinet.\n\nNote that this counts against the whole window height. With integer scaling the picture occupies\nonly part of it, so proportionally fewer scanlines land inside the picture - the actual number is\nshown below.'
    'How the phosphor is arranged:\n    Aperture grille - continuous vertical red/green/blue stripes, no vertical structure (Sony Trinitron)\n    Slot mask - stripes broken into short slots, staggered half a triad on alternate rows (common in Sega cabinets)\n    Shadow mask - a 2x2 dot triad with the fourth cell empty (cheaper colour monitors)\n\nSega cabinets mostly used the slot mask; choose it to match the original.'
    'How many physical pixels one phosphor dot spans.\n\nAt 4K a single pixel is finer than the eye can resolve, so it degrades into noise and the picture\ndarkens noticeably. At 4K use 2 (close up) or 3 (a large screen across the room). At 1080p,\n1 is about right.'
    'Strength of the brightness difference between the red, green and blue phosphors. Lower it to look\nmore like an ordinary monitor; raise it to look more like the grain of an old colour monitor with\nyour eye up against the glass.'
    'The mask and the scanlines both block light (a shadow mask passes roughly a third of it), so the\npicture comes out dim and dull. A real tube compensates with enormous peak brightness, which is\nwhy an arcade monitor looks so luminous.\n\nGain stands in for that here: raise it if turning the mask on makes the picture too dark.'
    'Simulates the soft halo light forms as it scatters inside the tube''s glass faceplate.\n\nOn a real CRT the bright areas bloom outwards, and that glassy translucency is what keeps the\npicture from reading as sharp pixels behind a grid, making it read as glowing glass instead.\n\nThe algorithm averages the bright areas only, so dark areas are not lifted and black stays black.\nIndependent of the CRT filter.'
    'How many downscaled levels the halo accumulates over. More levels spread the halo wider.\n\nEach level is half the resolution of the one above, so the count essentially sets the halo radius:\n3 levels is about 1/16 of the screen height, 6 is about 1/2 - very wide but very faint.\n\nAt 4K there are more pixels so the halo is relatively smaller; 5 is a good choice. At 1080p around\n4 is enough. Too high greys the whole screen and loses the glow-around-the-highlight look.'
    'Only areas brighter than this glow. This is what controls how clean the glow looks.\n\nLower: broad bright areas such as grass, snow and a bright sky glow too, lifting the whole picture\n       and giving it a washed-out, hazy look.\nHigher: only genuine highlights glow (headlights, metal glints, white lines) and the picture stays\n        clean.\n\nFor games with grass pitches or snow tracks, 60-70 is a good range. At night or in dark scenes\nlower it to around 30 so the lights bloom more visibly.'

    # --- UiTooltip (fullscreen modes, controls page) ---------------------------------------------
    'Trigger (brake) bindings depend on the input system:\n    XInput / SDL pads -> the triggers take one axis each, so the right trigger is JOY1_RZAXIS\n    DirectInput / rawinput / SDL -> both triggers share the Z axis, so the right trigger is JOY1_ZAXIS_POS\n\nAfter switching input systems the old bindings stop working and the brake does nothing.\nCurrently %s needs %s.'
    'Ordinary windowed fullscreen, presented through the desktop compositor.\nBest compatibility, but one extra composition step means the frame cadence is not locked to the\ndisplay and slight judder can show.'
    'Exclusive fullscreen plus G-SYNC: the display scans at the emulator''s frame rate, 1:1, removing\nframe-pacing judder and tearing. Needs a G-SYNC/FreeSync display and V-Sync turned on. No custom\nresolution required.\n\nThe Refresh rate setting on the Video page still applies here - it is the cadence the emulator\npresents at, and the display follows it. If your display does not support VRR, use the fixed\narcade refresh rate below instead.'
    'Drives the display at a fixed 57.524Hz, matching the Model 3''s original frame rate 1:1.\nYou must first create a custom 57.524Hz (or 57/58Hz) resolution in the NVIDIA control panel.\nWhen that mode is not found it falls back to exclusive fullscreen with G-SYNC/VRR automatically.\n\nThis takes priority over Refresh rate on the Video page, which is shown as 57.5 Hz and\ngreyed out. Use Scripts\\ArcadeDisplay.ps1 -Action List to check whether it is already supported;\n-Action Run switches automatically and restores the desktop on exit.'
    'Pads are rescanned automatically when plugged in or unplugged; you can also press Detect again\nto refresh manually.\n\nThis interface is navigated with the d-pad / stick, A to confirm and B to go back, which needs the\npad to be recognised by the system as a standard gamepad.'
    'The view the game starts in.\nThe default third-person chase view in Daytona 2 is VR3 (yellow).'
)

if ($keys.Count -ne $english.Count)
{
    Write-Error "Key/translation count mismatch: $($keys.Count) keys vs $($english.Count) translations."
    exit 1
}

# Running twice would insert a second copy of everything, and duplicate keys in the table are
# harmless to lookups but confusing to read. Bail out instead of silently doubling the table.
$marker = '// ---- Tooltip and help text'
if ([System.IO.File]::ReadAllText($locPath, $utf8).Contains($marker))
{
    Write-Error "The generated block is already present in Localization.cpp; nothing to do. Remove it first to regenerate."
    exit 1
}

$entries = @()
$entries += ''
$entries += '		// ---- Tooltip and help text ---------------------------------------------------------------'
$entries += '		// Long help strings, keyed on the exact text the code passes to Tr(). The keys below were'
$entries += '		// lifted out of Gui.cpp programmatically (Scripts/GenHelpTranslations.ps1) rather than retyped,'
$entries += '		// because a key that differs by one space simply falls through and shows Chinese.'
for ($i = 0; $i -lt $keys.Count; $i++)
{
    $entries += ('		{ "' + $keys[$i] + '",')
    $entries += ('		                                     "' + $english[$i] + '" },')
}

# Insert before the closing brace of kEnglish.
$lines = [System.Collections.Generic.List[string]]::new()
$lines.AddRange([System.IO.File]::ReadAllLines($locPath, $utf8))
$close = -1
for ($i = 0; $i -lt $lines.Count; $i++)
{
    if ($lines[$i] -match '^	\};')
    {
        $close = $i
        break
    }
}
if ($close -lt 0) { Write-Error 'Could not find the end of kEnglish.'; exit 1 }

$lines.InsertRange($close, [string[]]$entries)
[System.IO.File]::WriteAllLines($locPath, $lines, $utf8)

Write-Output "Inserted $($keys.Count) entries before line $($close + 1)."
