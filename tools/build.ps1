param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Rebuild,
    [switch]$EmbeddedDebugInfo,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputLabel = 'Verified',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$ToolsetVersion,
    [ValidateRange(1, 16)][int]$CompileProcesses = 4
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild)) { throw 'Visual Studio MSBuild was not found.' }
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
# One output tree per configuration/label; never mix diagnostic and ordinary objects.
$diagnosticSuffix = if ($EmbeddedDebugInfo) { 'Z7' } else { '' }
$buildName = $OutputLabel + $Configuration + $diagnosticSuffix
$logRoot = Join-Path $repoRoot 'logs\build'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$logPath = Join-Path $logRoot ($buildName + '_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '.log')
$arguments = @(
    (Join-Path $repoRoot 'DX22_01_plane.sln'),
    "/t:$target", "/p:Configuration=$Configuration", '/p:Platform=x64',
    "/p:IntDir=x64\$buildName\", "/p:OutDir=..\x64\$buildName\",
    "/p:Dx22MaxCompileProcesses=$CompileProcesses", '/m', '/v:normal', '/nologo'
)
if ($ToolsetVersion) { $arguments += "/p:VCToolsVersion=$ToolsetVersion" }
if ($EmbeddedDebugInfo) {
    $arguments += '/p:ForceImportAfterCppTargets=' + (Join-Path $PSScriptRoot 'release_without_compiler_pdb.targets')
}
$timer = [Diagnostics.Stopwatch]::StartNew()
Push-Location -LiteralPath $repoRoot
try {
    & $msbuild @arguments *> $logPath
    $buildExit = $LASTEXITCODE
}
finally { Pop-Location; $timer.Stop() }
Get-Content -LiteralPath $logPath -Tail 15
Write-Output ("Build {0}: exit={1}, seconds={2:N3}, log={3}" -f $buildName, $buildExit, $timer.Elapsed.TotalSeconds, $logPath)
if ($buildExit -ne 0) { exit $buildExit }
Write-Output ('Executable: ' + (Join-Path $repoRoot "x64\$buildName\DX22_01_plane.exe"))
