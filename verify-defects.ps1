[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Compiler,
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
    } catch { continue }
}
if (-not $taskPython) {
    Write-Error 'Python 3.10+ was not found. Add an existing Python interpreter to PATH.'
    exit 1
}
$taskArguments = @($taskPython.Prefix) + @('-X', 'utf8', (Join-Path $PSScriptRoot 'scripts/verify_defects.py'))
if ($Help) { $taskArguments += '--help' }
if ($Compiler) { $taskArguments += @('--compiler', $Compiler) }
if ($OutputDirectory) { $taskArguments += @('--output', $OutputDirectory) }
Write-Host ('Python: ' + $taskPython.Executable)
& $taskPython.Executable @taskArguments
exit $LASTEXITCODE
