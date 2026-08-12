<#
.SYNOPSIS
    Thin shim: forwards to the cross-platform run_tests.py (deprecated, will be removed next release).
#>
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PyExe = ""
$PyArgs = @()
$c = Get-Command python -ErrorAction SilentlyContinue
if ($c) { $PyExe = $c.Source }
else {
    $c = Get-Command py -ErrorAction SilentlyContinue
    if ($c) { $PyExe = $c.Source; $PyArgs = @("-3") }
}
if (-not $PyExe) {
    throw "python not found on PATH. Install Python 3.9+ or run the .py directly."
}
$script = Join-Path $ScriptDir "run_tests.py"
& $PyExe @PyArgs $script @args
exit $LASTEXITCODE
