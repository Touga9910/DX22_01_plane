param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('before', 'after', 'game_header_split_after', 'game_scene_split_after', 'game_scene_boundary_headers', 'game_route_split_after')]
    [string]$Phase,

    [int]$Runs = 3,

    [string[]]$Headers = @('Game.h', 'GameObject.h', 'TableConfig.h'),

    [string]$RepresentativeSource = 'GameSaveManager.cpp'
)

$ErrorActionPreference = 'Stop'
if ($Headers.Count -eq 1 -and $Headers[0].Contains(',')) {
    $Headers = @($Headers[0].Split(',') | ForEach-Object { $_.Trim() })
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectDir = Join-Path $repoRoot 'DX22_01_plane'
$solution = Join-Path $repoRoot 'DX22_01_plane.sln'
$project = Join-Path $projectDir 'DX22_01_plane.vcxproj'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
$outputRoot = Join-Path $repoRoot "build_measurements\$Phase"
$logRoot = Join-Path $outputRoot 'logs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found: $msbuild"
}

[xml]$projectXml = Get-Content -LiteralPath $project
$namespace = New-Object Xml.XmlNamespaceManager($projectXml.NameTable)
$namespace.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$projectSources = @(
    $projectXml.SelectNodes('//m:ClCompile[@Include]', $namespace) |
        ForEach-Object { $_.Include.Replace('/', '\') }
)
$authoredSources = @(
    $projectSources | Where-Object {
        -not $_.StartsWith('imgui\', [StringComparison]::OrdinalIgnoreCase) -and
        -not $_.Equals('stb_image.cpp', [StringComparison]::OrdinalIgnoreCase)
    }
)
$embeddedThirdPartySources = @(
    $projectSources | Where-Object { $_ -notin $authoredSources }
)

function Invoke-MSBuildLog {
    param(
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $arguments = @(
        $solution,
        "/t:$Target",
        '/p:Configuration=Debug',
        '/p:Platform=x64',
        '/m',
        '/v:normal',
        '/nologo'
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $msbuild @arguments 2>&1 | Out-File -LiteralPath $LogPath -Encoding utf8
    $exitCode = $LASTEXITCODE
    $timer.Stop()
    if ($exitCode -ne 0) {
        throw "MSBuild target $Target failed with exit code $exitCode. See $LogPath"
    }
    return [Math]::Round($timer.Elapsed.TotalSeconds, 3)
}

function Get-CompiledSources {
    param([Parameter(Mandatory = $true)][string]$LogPath)

    $lines = Get-Content -LiteralPath $LogPath
    $trimmed = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $lines) {
        [void]$trimmed.Add($line.Trim().Replace('/', '\'))
    }
    $compiled = @()
    foreach ($source in $projectSources) {
        $leaf = [IO.Path]::GetFileName($source)
        if ($trimmed.Contains($source) -or $trimmed.Contains($leaf)) {
            $compiled += $source
        }
    }
    return $compiled
}

function Touch-ProjectFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = [IO.Path]::GetFullPath((Join-Path $projectDir $RelativePath))
    if (-not $path.StartsWith($projectDir + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to touch outside project: $path"
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Touch target was not found: $path"
    }
    [IO.File]::SetLastWriteTimeUtc($path, [DateTime]::UtcNow)
}

function Invoke-StabilizingBuild {
    $path = Join-Path $logRoot '_stabilize.log'
    [void](Invoke-MSBuildLog -Target 'Build' -LogPath $path)
}

function New-Measurement {
    param(
        [Parameter(Mandatory = $true)][string]$Scenario,
        [Parameter(Mandatory = $true)][int]$Run,
        [Parameter(Mandatory = $true)][double]$Seconds,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $compiled = @(Get-CompiledSources -LogPath $LogPath)
    $authored = @($compiled | Where-Object { $_ -in $authoredSources })
    $thirdParty = @($compiled | Where-Object { $_ -in $embeddedThirdPartySources })
    return [pscustomobject][ordered]@{
        scenario = $Scenario
        run = $Run
        seconds = $Seconds
        compiled_cpp_count = $compiled.Count
        compiled_authored_cpp_count = $authored.Count
        compiled_embedded_third_party_cpp_count = $thirdParty.Count
        compiled_cpp = $compiled
        log = $LogPath.Substring($repoRoot.Length + 1).Replace('\', '/')
    }
}

$measurements = @()

for ($run = 1; $run -le $Runs; ++$run) {
    $cleanLog = Join-Path $logRoot ("clean-step-{0}.log" -f $run)
    [void](Invoke-MSBuildLog -Target 'Clean' -LogPath $cleanLog)
    $buildLog = Join-Path $logRoot ("clean-build-{0}.log" -f $run)
    $seconds = Invoke-MSBuildLog -Target 'Build' -LogPath $buildLog
    $measurements += New-Measurement `
        -Scenario 'clean_build' -Run $run -Seconds $seconds -LogPath $buildLog
}

for ($run = 1; $run -le $Runs; ++$run) {
    $buildLog = Join-Path $logRoot ("no_change-{0}.log" -f $run)
    $seconds = Invoke-MSBuildLog -Target 'Build' -LogPath $buildLog
    $measurements += New-Measurement `
        -Scenario 'no_change' -Run $run -Seconds $seconds -LogPath $buildLog
}

foreach ($header in $Headers) {
    $scenario = 'touch_' + [IO.Path]::GetFileNameWithoutExtension($header).ToLowerInvariant()
    for ($run = 1; $run -le $Runs; ++$run) {
        Invoke-StabilizingBuild
        Touch-ProjectFile -RelativePath $header
        $buildLog = Join-Path $logRoot ("{0}-{1}.log" -f $scenario, $run)
        $seconds = Invoke-MSBuildLog -Target 'Build' -LogPath $buildLog
        $measurements += New-Measurement `
            -Scenario $scenario -Run $run -Seconds $seconds -LogPath $buildLog
    }
}

for ($run = 1; $run -le $Runs; ++$run) {
    Invoke-StabilizingBuild
    Touch-ProjectFile -RelativePath $RepresentativeSource
    $buildLog = Join-Path $logRoot ("touch_cpp-{0}.log" -f $run)
    $seconds = Invoke-MSBuildLog -Target 'Build' -LogPath $buildLog
    $measurements += New-Measurement `
        -Scenario 'touch_cpp' -Run $run -Seconds $seconds -LogPath $buildLog
}

$summaries = @()
foreach ($group in ($measurements | Group-Object scenario)) {
    $times = @($group.Group.seconds | Sort-Object)
    $authoredCounts = @($group.Group.compiled_authored_cpp_count | Sort-Object)
    $middle = [int][Math]::Floor($times.Count / 2)
    $summaries += [ordered]@{
        scenario = $group.Name
        median_seconds = $times[$middle]
        minimum_seconds = $times[0]
        maximum_seconds = $times[-1]
        spread_seconds = [Math]::Round($times[-1] - $times[0], 3)
        median_authored_cpp_count = $authoredCounts[$middle]
        runs = $group.Count
    }
}

$result = [ordered]@{
    phase = $Phase
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
    visual_studio = 'Visual Studio 2022 Enterprise 17.14.51'
    msbuild = '17.14.51.32402'
    configuration = 'Debug|x64'
    msbuild_parallel = '/m (maximum nodes auto)'
    compiler_parallel = '/MP via MultiProcessorCompilation=true'
    logical_processors = [Environment]::ProcessorCount
    precompiled_header = 'Not configured'
    solution = 'DX22_01_plane.sln'
    project = 'DX22_01_plane/DX22_01_plane.vcxproj'
    headers = $Headers
    representative_source = $RepresentativeSource
    authored_cpp_count = $authoredSources.Count
    embedded_third_party_cpp_count = $embeddedThirdPartySources.Count
    summaries = $summaries
    measurements = $measurements
}

$summaryPath = Join-Path $outputRoot 'summary.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summaries | Format-Table -AutoSize
Write-Output "Saved $summaryPath"
