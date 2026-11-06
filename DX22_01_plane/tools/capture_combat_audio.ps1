param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Label,
    [ValidateRange(0.0, 1.0)][double]$SeVolume = 0.8,
    [switch]$UseFormalAssetsOnly
)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$captureRoot = Join-Path $project 'tools\runtime_tests\combat_audio'
$folder = Join-Path $captureRoot $Label
if (Test-Path -LiteralPath $folder) {
    throw "Use a fresh capture label: $folder"
}

function Write-JsonAtomic([string]$Path, [object]$Value) {
    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    $temporary = "$Path.writing"
    [IO.File]::WriteAllText(
        $temporary,
        ($Value | ConvertTo-Json -Depth 100),
        [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Read-JsonSafe([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 |
            ConvertFrom-Json
    }
    catch { return $null }
}

function Wait-State([scriptblock]$Predicate, [Diagnostics.Process]$Process, [int]$TimeoutSeconds = 90) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw "Test game exited early: $($Process.ExitCode)"
        }
        $value = & $Predicate
        if ($null -ne $value) { return $value }
        Start-Sleep -Milliseconds 40
    }
    throw 'Timed out waiting for the game state.'
}

New-Item -ItemType Directory -Path $folder | Out-Null
New-Item -ItemType Directory -Path (Join-Path $folder 'assets') | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'assets\data') `
    -Destination (Join-Path $folder 'assets\data') -Recurse
Copy-Item -LiteralPath (Join-Path $project 'shader') `
    -Destination (Join-Path $folder 'shader') -Recurse
Get-ChildItem -LiteralPath $project -Filter '*.cso' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $folder
}
foreach ($resource in @('model', 'texture', 'fonts', 'audio')) {
    $source = Join-Path $project "assets\$resource"
    if (Test-Path -LiteralPath $source) {
        New-Item -ItemType Junction `
            -Path (Join-Path $folder "assets\$resource") `
            -Target $source | Out-Null
    }
}

$bridgeConfigPath = Join-Path $folder 'assets\data\game_mcp_bridge.json'
$bridgeConfig = [ordered]@{
    enabled = $true
    allow_write_actions = $true
    bridge_directory = 'runtime/game_mcp'
    state_publish_interval_frames = 60
    command_poll_interval_frames = 1
}
Write-JsonAtomic $bridgeConfigPath $bridgeConfig

$settings = [ordered]@{
    bgm_volume = 0.0
    se_volume = $SeVolume
    vibration_enabled = $false
    screen_flash_enabled = $true
    camera_shake_enabled = $true
    collision_impact_effects_enabled = $true
    heavy_chain_effects_enabled = $true
    enemy_hit_flash_enabled = $true
    defeat_vfx_enabled = $true
    multi_defeat_effects_enabled = $true
    frame_time_measurement_enabled = $true
    decorative_ring_amount = 2
    fullscreen = $false
    resolution_index = 1
}
Write-JsonAtomic (Join-Path $folder 'saves\settings.json') $settings

$presetPath = Join-Path $project 'tools\runtime_tests\debug_mode\setup_win.json'
$preset = Get-Content -LiteralPath $presetPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
$ball = $preset.deck[0]
$ball.definition_id = 'player_heavy'
$ball.category = 'heavy'
$ball.upgrade_level = 0
$ball.status.attack = 2
$ball.status.mass = 4.0
$ball.status.knockbackTransfer = 1.0
$ball.status.abilities.heavyDistanceStacks = $true
$preset.deck = @($ball)
$preset.seed = 20260904
$preset.player_x = 0.0
$preset.player_z = 0.0
for ($index = 0; $index -lt $preset.relics.Count; ++$index) {
    $preset.relics[$index] = $false
}
$preset.selected_ascension = 0
$preset.highest_unlocked_ascension = 0
$preset.enemy_validation.enabled = $true
$preset.enemy_validation.hp_multiplier = 1.5
$preset.enemy_validation.enemy_count = 8
$preset.enemy_validation.layout_pattern = 'dense'
Write-JsonAtomic (Join-Path $folder 'saves\debug_battle_setup.json') $preset

$statePath = Join-Path $folder 'runtime\game_mcp\game_state.json'
$resultPath = Join-Path $folder 'runtime\game_mcp\last_result.json'
$logPath = Join-Path $folder 'process.log'
$gameArguments = @('--debug-battle')
if (-not $UseFormalAssetsOnly) { $gameArguments += '--audio-test-tones' }
$process = Start-Process -FilePath ([IO.Path]::GetFullPath($Exe)) `
    -ArgumentList $gameArguments `
    -WorkingDirectory $folder -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $logPath -RedirectStandardError (Join-Path $folder 'process_error.log')
try {
    $initial = Wait-State {
        $state = Read-JsonSafe $statePath
        if ($null -ne $state -and $state.debug_mode.active -and
            $state.available_actions -contains 'fire_shot') { return $state }
        return $null
    } $process 90
    Write-JsonAtomic (Join-Path $folder 'initial.json') $initial

    $commandId = [Guid]::NewGuid().ToString('N')
    $command = [ordered]@{
        command_id = $commandId
        action = 'fire_shot'
        arguments = [ordered]@{
            direction_x = -0.9916810938126976
            direction_z = -0.1287191057087161
            power = 8
        }
    }
    Write-JsonAtomic (Join-Path $folder 'runtime\game_mcp\pending_command.json') $command
    Wait-State {
        $result = Read-JsonSafe $resultPath
        if ($null -ne $result -and $result.command_id -eq $commandId) {
            if (-not $result.ok) { throw ($result | ConvertTo-Json -Depth 20) }
            return $result
        }
        return $null
    } $process 30 | Out-Null

    $final = Wait-State {
        $state = Read-JsonSafe $statePath
        if ($null -ne $state -and $null -ne $state.latest_shot_physics_chain -and
            (($state.available_actions -contains 'fire_shot') -or
             $state.debug_mode.finished)) { return $state }
        return $null
    } $process 120
    Write-JsonAtomic (Join-Path $folder 'final.json') $final
    $summary = [ordered]@{
        label = $Label
        executable = [IO.Path]::GetFullPath($Exe)
        se_volume = $SeVolume
        generated_test_tones = -not $UseFormalAssetsOnly
        direction_x = -0.9916810938126976
        direction_z = -0.1287191057087161
        power = 8
        physics_chain = $final.latest_shot_physics_chain
        frame_timing = $final.frame_timing
        audio_feedback = $final.audio_feedback
        collision_vfx = $final.collision_vfx
    }
    Write-JsonAtomic (Join-Path $folder 'summary.json') $summary
    $summary | ConvertTo-Json -Depth 30
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Refresh()
        if (-not $process.CloseMainWindow()) {
            $process.Kill()
        }
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
