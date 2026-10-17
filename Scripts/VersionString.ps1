# Prints this build's version string, e.g. "v1.0.3a-20261013".
#
# Read from Src/Version.h rather than written here as well: the number is recorded once, in that
# header, and compiled into the executable. A second copy in a build script is a copy that will
# eventually be out of step with the one the program reports - and then "which build is this?" has two
# answers. build.bat calls this to label what it just assembled.
#
# Kept as a script rather than inlined into build.bat because cmd's quoting cannot express this
# without a fight: the command needs a quoted path, a pattern containing a space, and quote-delimited
# tokens all at once, and the resulting one-liner is unreadable and breaks in ways that look like
# something else.
$root = Split-Path -Parent $PSScriptRoot
$header = Join-Path $root 'Src/Version.h'

$ver = ''
$date = ''
if (Test-Path $header)
{
    foreach ($line in (Get-Content $header))
    {
        # Anchored to the start of the line, and to the name being the thing defined. Without the
        # anchor this reads the wrong line: SUPERMODEL_TITLE_FULL is built by pasting the two names
        # together with a literal "-" between them, and since the last match wins, that line's "-"
        # silently replaced the real version with a dash.
        if ($line -match '^\s*#define\s+SUPERMODEL_FORK_VERSION\s+"([^"]+)"') { $ver = $Matches[1] }
        if ($line -match '^\s*#define\s+SUPERMODEL_FORK_DATE\s+"([^"]+)"') { $date = $Matches[1] }
    }
}

if ($ver -and $date) { Write-Output ("v{0}-{1}" -f $ver, $date) }
else { Write-Output "v?.? (Src/Version.h did not yield a version)" }
