[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Cpp,
    [string]$OutputDirectory,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
$taskCandidates = @()
foreach ($taskName in @('python', 'python3', 'py')) {
    $taskCommand = Get-Command $taskName -CommandType Application -ErrorAction SilentlyContinue
    if ($taskCommand) {
        $taskPrefix = @()
        if ($taskName -eq 'py') { $taskPrefix = @('-3') }
        $taskCandidates += @{ Executable = $taskCommand.Source; Prefix = $taskPrefix }
    }
}
# Use the existing desktop Python runtime when Python is not on PATH.
# This fallback is optional; no downloads or environment changes are made.
if ($env:USERPROFILE) {
    $taskBundledPython = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    if (Test-Path -LiteralPath $taskBundledPython -PathType Leaf) {
        $taskCandidates += @{ Executable = $taskBundledPython; Prefix = @() }
    }
}

$taskPython = $null
foreach ($taskCandidate in $taskCandidates) {
    $taskProbe = @($taskCandidate.Prefix) + @('-c', 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)')
    try {
        & $taskCandidate.Executable @taskProbe 2>$null
        if ($LASTEXITCODE -eq 0) { $taskPython = $taskCandidate; break }
    } catch {
        continue
    }
}
if (-not $taskPython) {
    Write-Error 'Python 3.10+ was not found. Install Python or add an existing Python interpreter to PATH, then run .\verify.ps1 again.'
    exit 1
}

$taskArguments = @($taskPython.Prefix) + @((Join-Path $PSScriptRoot 'scripts/verify_beverage_full.py'))
if ($Help) { $taskArguments += '--help' }
if ($Cpp) { $taskArguments += '--cpp' }
if ($OutputDirectory) { $taskArguments += @('--output', $OutputDirectory) }
Write-Host ('Python: ' + $taskPython.Executable)
& $taskPython.Executable @taskArguments
exit $LASTEXITCODE
