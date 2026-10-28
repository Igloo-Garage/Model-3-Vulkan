# Prints this build's version string, e.g. "v1.2.3a".
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
if (Test-Path $header)
{
    foreach ($line in (Get-Content $header))
    {
        # Anchored to the start of the line, and to the name being the thing defined: other lines in
        # the header mention SUPERMODEL_FORK_VERSION (SUPERMODEL_TITLE_FULL is assembled from it), and
        # an unanchored match could read the wrong one.
        if ($line -match '^\s*#define\s+SUPERMODEL_FORK_VERSION\s+"([^"]+)"') { $ver = $Matches[1] }
    }
}

if ($ver) { Write-Output ("v{0}" -f $ver) }
else { Write-Output "v?.? (Src/Version.h did not yield a version)" }
