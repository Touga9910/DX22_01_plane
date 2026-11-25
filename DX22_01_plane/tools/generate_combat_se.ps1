param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\audio\combat'),
    [string]$BilliardSourcePath = (Join-Path $PSScriptRoot '..\assets\audio\combat\source\billiard_ball_clack_539854.wav')
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

function Read-BilliardRecording {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'RIFF' -or
        [Text.Encoding]::ASCII.GetString($bytes, 8, 4) -ne 'WAVE') {
        throw 'The billiard source must be a RIFF/WAVE PCM recording.'
    }
    $formatFound = $false
    $dataOffset = -1
    $dataLength = 0
    for ($offset = 12; $offset + 8 -le $bytes.Length;) {
        $chunk = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [BitConverter]::ToUInt32($bytes, $offset + 4)
        $offset += 8
        if ($size -gt $bytes.Length - $offset) { throw 'Truncated billiard source chunk.' }
        if ($chunk -eq 'fmt ') {
            if ($size -lt 16 -or [BitConverter]::ToUInt16($bytes, $offset) -ne 1 -or
                [BitConverter]::ToUInt16($bytes, $offset + 2) -ne 1 -or
                [BitConverter]::ToUInt32($bytes, $offset + 4) -ne $script:sampleRate -or
                [BitConverter]::ToUInt16($bytes, $offset + 12) -ne 2 -or
                [BitConverter]::ToUInt16($bytes, $offset + 14) -ne 16) {
                throw 'The billiard source must be 48 kHz, 16-bit, mono PCM.'
            }
            $formatFound = $true
        }
        elseif ($chunk -eq 'data') {
            $dataOffset = $offset
            $dataLength = $size
        }
        $offset += $size + ($size -band 1)
    }
    if (-not $formatFound -or $dataOffset -lt 0 -or $dataLength -le 0 -or ($dataLength -band 1)) {
        throw 'The billiard source has no valid PCM sample data.'
    }
    $samples = New-Object 'System.Double[]' ($dataLength / 2)
    for ($index = 0; $index -lt $samples.Length; ++$index) {
        $samples[$index] = [BitConverter]::ToInt16($bytes, $dataOffset + $index * 2) / 32768.0
    }
    return ,$samples
}

function New-BilliardImpact {
    param([string]$FileName, [double]$DurationSeconds, [double]$TargetPeak, [string]$Purpose)
    $sampleCount = [int][Math]::Round($DurationSeconds * $script:sampleRate)
    if ($sampleCount -gt $script:billiardRecording.Length) { throw 'The billiard recording is too short.' }
    $samples = New-Object 'System.Double[]' $sampleCount
    $mean = 0.0
    for ($index = 0; $index -lt $sampleCount; ++$index) { $mean += $script:billiardRecording[$index] }
    $mean /= $sampleCount
    $fadeInSamples = [int]($script:sampleRate * 0.00025)
    $fadeOutSamples = [int]($script:sampleRate * 0.012)
    for ($index = 0; $index -lt $sampleCount; ++$index) {
        $fadeIn = [Math]::Min(1.0, $index / [double]$fadeInSamples)
        $fadeOut = [Math]::Min(1.0, ($sampleCount - 1 - $index) / [double]$fadeOutSamples)
        $samples[$index] = ($script:billiardRecording[$index] - $mean) * $fadeIn * $fadeOut
    }
    $peak = Write-PcmWav (Join-Path $OutputDirectory $FileName) $samples $TargetPeak
    return [ordered]@{
        file = $FileName
        purpose = $Purpose
        duration_seconds = $DurationSeconds
        sample_rate_hz = $script:sampleRate
        channels = 1
        bits_per_sample = 16
        peak_linear = [Math]::Round($peak, 6)
        peak_dbfs = [Math]::Round(20.0 * [Math]::Log10($peak), 3)
        method = 'CC0 billiard recording: trim, DC removal, 0.25 ms fade-in, 12 ms fade-out, peak normalization'
        source = 'source/billiard_ball_clack_539854.wav'
        generator = 'tools/generate_combat_se.ps1'
        author = 'Za-Games'
        original_title = 'billiard ball clack'
        source_page = 'https://freesound.org/people/Za-Games/sounds/539854/'
        license = 'CC0 1.0 Universal'
        license_url = 'https://creativecommons.org/publicdomain/zero/1.0/'
        source_origin = 'Public HQ MP3 preview decoded and linearly resampled to 48 kHz PCM; original WAV not downloaded'
        preview_url = 'https://cdn.freesound.org/previews/539/539854_12029332-hq.mp3'
        preview_sha256 = '8fba3f6faccc03d06ac6fa7f1a3e496b7dba18468364082526a9b8b52af0950d'
        source_pcm_sha256 = $script:billiardSourceHash
        status = 'Real-recording replacement; human audition required'
    }
}

function New-CombatSound {
    param(
        [string]$FileName,
        [ValidateSet('defeat', 'wall', 'chain3', 'chain6', 'chain10', 'chain15')]
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

$billiardSourceHash = (Get-FileHash -LiteralPath $BilliardSourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($billiardSourceHash -ne 'e2e14a8910278f3559d5f0a86d8e098fb8d7c4f78bc7221e9e93d2beaa9064e7') {
    throw 'The billiard source changed. Verify its provenance and update the recorded source hash before regenerating.'
}
$billiardRecording = Read-BilliardRecording $BilliardSourcePath

if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$manifest = @()
$manifest += New-BilliardImpact 'direct_impact.wav' 0.180 0.74 'Player-to-enemy billiard-ball impact'
$manifest += New-BilliardImpact 'enemy_impact.wav' 0.160 0.58 'Enemy-to-enemy billiard-ball impact'
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
$manifest | ForEach-Object { [pscustomobject]$_ } | Format-Table file, duration_seconds, peak_dbfs, purpose
