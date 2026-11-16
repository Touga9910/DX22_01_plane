param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Label,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('true', 'false')]
    [string]$UseSharedPch = 'true',
    [ValidateRange(1, 10)]
    [int]$Runs = 3,
    [ValidateSet('Rebuild', 'Build')]
    [string]$Target = 'Rebuild'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $repoRoot "logs\clean_build_optimization\$Label"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$devCommand = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
$targets = Join-Path $PSScriptRoot 'clean_build_diagnostics.targets'

# Before and after use the same output directories, compiler, diagnostics,
# parallelism and optimization. Rebuild regenerates shaders, objects and PCH.
$command = 'call "' + $devCommand + '" -arch=x64 -host_arch=x64 -vcvars_ver=14.44 && "' +
    $msbuild + '" DX22_01_plane.sln /t:' + $Target + ' /p:Configuration=' + $Configuration +
    ' /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:Dx22UseSharedPch=' + $UseSharedPch +
    ' /p:IntDir=x64\CleanBuild' + $Configuration + '\ /p:OutDir=..\x64\CleanBuild' + $Configuration +
    '\ /p:ForceImportAfterCppTargets="' + $targets + '" /m /v:normal /nologo /clp:PerformanceSummary'

$measurements = @()
Push-Location -LiteralPath $repoRoot
try {
    for ($run = 1; $run -le $Runs; ++$run) {
        $logPath = Join-Path $outputRoot ("build-{0}.log" -f $run)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        cmd.exe /d /s /c $command *> $logPath
        $exitCode = $LASTEXITCODE
        $timer.Stop()
        $log = Get-Content -LiteralPath $logPath -Raw
        $compiled = @([regex]::Matches($log, '(?m)^\s*(?:\d+>)?([A-Za-z_][\w]*\.cpp)\s*$') |
            ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        $frontend = @([regex]::Matches($log, 'time\([^\r\n]*c1xx\.dll\)\s*=\s*([0-9.]+)s') |
            ForEach-Object { [double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        $backend = @([regex]::Matches($log, 'time\([^\r\n]*c2\.dll\)\s*=\s*([0-9.]+)s') |
            ForEach-Object { [double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        $row = [pscustomobject][ordered]@{
            label = $Label
            configuration = $Configuration
            target = $Target
            use_shared_pch = $UseSharedPch
            run = $run
            seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            exit_code = $exitCode
            compiled_cpp_count = $compiled.Count
            compiled_cpp = $compiled
            frontend_sum_seconds = ($frontend | Measure-Object -Sum).Sum
            backend_sum_seconds = ($backend | Measure-Object -Sum).Sum
            frontend_samples = $frontend.Count
            backend_samples = $backend.Count
            log = $logPath.Substring($repoRoot.Length + 1).Replace('\', '/')
        }
        $measurements += $row
        ConvertTo-Json -InputObject @($measurements) -Depth 5 |
            Set-Content -LiteralPath (Join-Path $outputRoot 'measurements.json') -Encoding utf8
        $measurements | Select-Object label, configuration, target, use_shared_pch, run, seconds,
            exit_code, compiled_cpp_count, frontend_sum_seconds, backend_sum_seconds,
            frontend_samples, backend_samples, log |
            Export-Csv -LiteralPath (Join-Path $outputRoot 'measurements.csv') -NoTypeInformation -Encoding utf8
        $row | Select-Object label, configuration, run, seconds, exit_code, compiled_cpp_count,
            frontend_sum_seconds, backend_sum_seconds | Format-List
        if ($exitCode -ne 0) { throw "Build failed. See $logPath" }
    }
}
finally {
    Pop-Location
}
