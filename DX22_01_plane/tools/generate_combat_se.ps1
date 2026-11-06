param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\audio\combat')
)

$ErrorActionPreference = 'Stop'
$sampleRate = 48000
$twoPi = 2.0 * [Math]::PI

function Get-BellSample {
    param(
        [double]$Time,
        [double]$Start,
        [double]$Frequency,
        [double]$Decay
    )
    $localTime = $Time - $Start
    if ($localTime -lt 0.0) { return 0.0 }
    $attack = [Math]::Min(1.0, $localTime * 180.0)
    $envelope = $attack * [Math]::Exp(-$Decay * $localTime)
    return $envelope * (
        [Math]::Sin($script:twoPi * $Frequency * $localTime) +
        0.36 * [Math]::Sin($script:twoPi * $Frequency * 2.01 * $localTime) +
        0.14 * [Math]::Sin($script:twoPi * $Frequency * 3.98 * $localTime))
}

function Write-PcmWav {
    param(
        [string]$Path,
        [double[]]$Samples,
        [double]$TargetPeak
    )
    $rawPeak = 0.0
    foreach ($sample in $Samples) {
        $rawPeak = [Math]::Max($rawPeak, [Math]::Abs($sample))
    }
    if ($rawPeak -le 0.0) { throw "Generated silence: $Path" }
    $scale = $TargetPeak / $rawPeak
    $pcm = New-Object 'System.Int16[]' $Samples.Length
    $measuredPeak = 0.0
    for ($index = 0; $index -lt $Samples.Length; ++$index) {
        $normalized = [Math]::Max(-1.0, [Math]::Min(1.0, $Samples[$index] * $scale))
        $measuredPeak = [Math]::Max($measuredPeak, [Math]::Abs($normalized))
        $pcm[$index] = [int16][Math]::Round($normalized * 32767.0)
    }

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Create, [IO.FileAccess]::Write)
    try {
        $writer = [IO.BinaryWriter]::new($stream)
        $dataBytes = $pcm.Length * 2
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([uint32](36 + $dataBytes))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('fmt '))
        $writer.Write([uint32]16)
        $writer.Write([uint16]1)
        $writer.Write([uint16]1)
        $writer.Write([uint32]$script:sampleRate)
        $writer.Write([uint32]($script:sampleRate * 2))
        $writer.Write([uint16]2)
        $writer.Write([uint16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([uint32]$dataBytes)
        foreach ($value in $pcm) { $writer.Write($value) }
        $writer.Flush()
    }
    finally {
        $stream.Dispose()
    }
    return $measuredPeak
}

function New-CombatSound {
    param(
        [string]$FileName,
        [ValidateSet('direct', 'enemy', 'defeat', 'wall', 'chain3', 'chain6', 'chain10', 'chain15')]
        [string]$Kind,
        [double]$DurationSeconds,
        [double]$TargetPeak,
        [int]$Seed,
        [string]$Purpose
    )
    $sampleCount = [int][Math]::Round($DurationSeconds * $script:sampleRate)
    $samples = New-Object 'System.Double[]' $sampleCount
    $random = [Random]::new($Seed)
    $previousNoise = 0.0
    $chainNotes = switch ($Kind) {
        'chain3'  { @(@(0.000, 620.0), @(0.065, 830.0)) }
        'chain6'  { @(@(0.000, 620.0), @(0.060, 830.0), @(0.120, 1040.0)) }
        'chain10' { @(@(0.000, 620.0), @(0.052, 830.0), @(0.104, 1040.0), @(0.156, 1320.0)) }
        'chain15' { @(@(0.000, 620.0), @(0.046, 830.0), @(0.092, 1040.0), @(0.138, 1320.0), @(0.184, 1660.0)) }
        default   { @() }
    }
    for ($index = 0; $index -lt $sampleCount; ++$index) {
        $time = $index / [double]$script:sampleRate
        $noise = $random.NextDouble() * 2.0 - 1.0
        $highNoise = $noise - 0.76 * $previousNoise
        $previousNoise = $noise
        $value = 0.0
        switch ($Kind) {
            'direct' {
                $bodyPhase = $script:twoPi * (158.0 * $time - 210.0 * $time * $time)
                $body = [Math]::Sin($bodyPhase) * [Math]::Exp(-17.0 * $time)
                $attack = $highNoise * [Math]::Exp(-68.0 * $time)
                $knock = [Math]::Sin($script:twoPi * 760.0 * $time) * [Math]::Exp(-38.0 * $time)
                $value = 0.74 * $body + 0.42 * $attack + 0.24 * $knock
            }
            'enemy' {
                $click = $highNoise * [Math]::Exp(-92.0 * $time)
                $shell = ([Math]::Sin($script:twoPi * 430.0 * $time) +
                    0.34 * [Math]::Sin($script:twoPi * 970.0 * $time)) *
                    [Math]::Exp(-45.0 * $time)
                $value = 0.48 * $click + 0.52 * $shell
            }
            'defeat' {
                $success = Get-BellSample $time 0.000 720.0 11.5
                $lift = Get-BellSample $time 0.052 960.0 12.5
                $sparkle = Get-BellSample $time 0.106 1440.0 15.0
                $value = 0.44 * $success + 0.39 * $lift + 0.25 * $sparkle +
                    0.08 * $highNoise * [Math]::Exp(-20.0 * $time)
            }
            'wall' {
                $tap = $highNoise * [Math]::Exp(-105.0 * $time)
                $wood = [Math]::Sin($script:twoPi * 285.0 * $time) *
                    [Math]::Exp(-62.0 * $time)
                $value = 0.55 * $tap + 0.36 * $wood
            }
            default {
                foreach ($note in $chainNotes) {
                    $value += 0.34 * (Get-BellSample $time $note[0] $note[1] 10.5)
                }
                if ($Kind -eq 'chain10' -or $Kind -eq 'chain15') {
                    $value += 0.24 * [Math]::Sin($script:twoPi * 118.0 * $time) *
                        [Math]::Exp(-13.0 * $time)
                }
                if ($Kind -eq 'chain15') {
                    $value += 0.11 * $highNoise * [Math]::Exp(-8.0 * $time)
                    $value += 0.14 * (Get-BellSample $time 0.184 1245.0 6.8)
                }
            }
        }
        $attackEnvelope = [Math]::Min(1.0, $time * 500.0)
        $fadeStart = [Math]::Max(0.0, $DurationSeconds - 0.012)
        $fadeOut = if ($time -gt $fadeStart) {
            [Math]::Max(0.0, ($DurationSeconds - $time) / ($DurationSeconds - $fadeStart))
        } else { 1.0 }
        $samples[$index] = $value * $attackEnvelope * $fadeOut
    }
    $path = Join-Path $OutputDirectory $FileName
    $peak = Write-PcmWav $path $samples $TargetPeak
    return [ordered]@{
        file = $FileName
        purpose = $Purpose
        duration_seconds = [Math]::Round($sampleCount / [double]$script:sampleRate, 6)
        sample_rate_hz = $script:sampleRate
        channels = 1
        bits_per_sample = 16
        peak_linear = [Math]::Round($peak, 6)
        peak_dbfs = [Math]::Round(20.0 * [Math]::Log10($peak), 3)
        method = 'Original deterministic layered PCM synthesis'
        source = 'tools/generate_combat_se.ps1'
        license = 'Project-original; no third-party audio used'
        status = 'Formal adoption candidate; human audition required'
    }
}

if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$manifest = @()
$manifest += New-CombatSound 'direct_impact.wav' 'direct' 0.180 0.82 4101 'Player-to-enemy direct impact'
$manifest += New-CombatSound 'enemy_impact.wav' 'enemy' 0.090 0.55 4102 'Enemy-to-enemy chain impact'
$manifest += New-CombatSound 'enemy_defeat.wav' 'defeat' 0.320 0.70 4103 'Unique enemy defeat'
$manifest += New-CombatSound 'wall_impact.wav' 'wall' 0.065 0.42 4104 'Accepted wall reflection'
$manifest += New-CombatSound 'chain_3.wav' 'chain3' 0.240 0.64 4203 'CHAIN 3 milestone'
$manifest += New-CombatSound 'chain_6.wav' 'chain6' 0.300 0.68 4206 'CHAIN 6 milestone'
$manifest += New-CombatSound 'chain_10.wav' 'chain10' 0.380 0.76 4210 'CHAIN 10 milestone'
$manifest += New-CombatSound 'chain_15.wav' 'chain15' 0.460 0.80 4215 'CHAIN 15 milestone'
Copy-Item -LiteralPath (Join-Path $OutputDirectory 'chain_3.wav') `
    -Destination (Join-Path $OutputDirectory 'chain_milestone.wav') -Force
$common = $manifest | Where-Object { $_.file -eq 'chain_3.wav' } | Select-Object -First 1
$manifest += [ordered]@{
    file = 'chain_milestone.wav'
    purpose = 'Fallback for any missing CHAIN stage asset'
    duration_seconds = $common.duration_seconds
    sample_rate_hz = $common.sample_rate_hz
    channels = $common.channels
    bits_per_sample = $common.bits_per_sample
    peak_linear = $common.peak_linear
    peak_dbfs = $common.peak_dbfs
    method = 'Byte-identical fallback copy of chain_3.wav'
    source = 'tools/generate_combat_se.ps1'
    license = 'Project-original; no third-party audio used'
    status = 'Formal adoption candidate; human audition required'
}
[IO.File]::WriteAllText(
    (Join-Path $OutputDirectory 'manifest.json'),
    ($manifest | ConvertTo-Json -Depth 10),
    [Text.UTF8Encoding]::new($false))
$manifest | Format-Table file, duration_seconds, peak_dbfs, purpose
