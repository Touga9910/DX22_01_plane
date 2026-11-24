#include "Game.h"
#pragma execution_character_set("utf-8")
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameStageOverrideState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressController.h"
#include "BossShotPlanner.h"
#include "GameDebugController.h"
#include "GameDebugHost.h"
#include "DebugRunPreset.h"
#include "json/json.hpp"
#include "GameUi.h"
#include "GameMcpBridge.h"
#include "BalanceLogger.h"
#include "EnemyBall.h"
#include "EnemyValidationLayout.h"
#include "PlayerBall.h"
#include "PlayerBallText.h"
#include "EnemyText.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{
    /// <summary>
    /// 指定した球種IDの表示名を取得する。
    /// </summary>
    const char* BallLabel(const std::string& id)
    {
        return PlayerBallText::GetName(id);
    }
    /// <summary>
    /// ステージIDを表示名に変換し、未指定なら自動選択の表示を返す。
    /// </summary>
    std::string RunStageLabel(const std::vector<StageData>& stages, const std::string& id)
    {
        if (id.empty()) return "進行度から自動選択";
        const auto* stage = StageDataLoader::FindById(stages, id);
        return stage ? stage->DisplayName() : id;
    }

    /// <summary>
    /// 球の攻撃、防御、質量などの能力値を編集するスライダーを表示する。
    /// </summary>
    void EditStatus(BallStatus& s, bool includeDefense)
    {
        ImGui::SliderInt("攻撃", &s.attack, 0, 999, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (includeDefense)
			ImGui::SliderInt("防御", &s.defense, 0, 999, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("質量", &s.mass, 0.1f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("半径", &s.radius, 0.1f, 12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("反発係数", &s.restitution, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("摩擦", &s.friction, 0.001f, 0.2f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("押し出し倍率", &s.knockbackTransfer, 0, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (s.abilities.pierce)
        {
            ImGui::SliderInt("貫通回数", &s.pierceMaxUses, 0, 16);
            ImGui::SliderFloat("貫通後の速度維持率", &s.pierceSpeedRetention, 0, 1);
        }
        if (s.abilities.anchor)
        {
            ImGui::SliderFloat("停止への減速倍率", &s.anchorBrakeMultiplier, 1, 5);
            ImGui::SliderFloat("停止速度の二乗", &s.anchorStopSpeedSquared, 0.03f, 1);
            ImGui::Checkbox("停止中の押し戻し無効", &s.anchorKnockbackImmune);
        }
    }
    const std::filesystem::path PresetPath = "saves/debug_battle_setup.json";
    const std::filesystem::path RunPresetsPath = "saves/debug_run_presets.json";
}

/// <summary>
/// タイトル画面でデバッグエディターを開けるかを返す。
/// </summary>
bool GameDebugHost::CanOpenEditor(const Game& game)
{
    return game.GetCurrentSceneType() == SceneType::Title;
}

/// <summary>
/// デバッグ用の無限所持金を切り替える。
/// </summary>
void GameDebugHost::SetInfiniteMoney(Game& game, bool enabled)
{
    game.m_RunController->SetDebugInfiniteMoney(enabled);
}

/// <summary>
/// 指定した設定と乱数シードでデバッグランを開始する。
/// </summary>
void GameDebugHost::StartRun(Game& game, const DebugRunSetup& setup, std::uint32_t seed)
{
    game.m_StageOverrideState->ClearQueued();
    game.m_BossShotPlanner->Reset();
    game.StartNewRun("debug_sandbox", "debug_run", "", "", seed, "", setup.combatMode, PhaseRunProgressionMode::RouteProgress);
}

/// <summary>
/// デバッグランの進行先を、指定した進行度と行き先に切り替える。
/// </summary>
bool GameDebugHost::JumpRun(Game& game, const DebugRunSetup& setup)
{
    if (!game.IsDebugRunMode() || !game.IsRunSessionActive()) return false;
    std::optional<StageData> selected;
    const bool battle = setup.destination == DebugRunDestination::Battle ||
        setup.destination == DebugRunDestination::MidBoss || setup.destination == DebugRunDestination::FinalBoss;
    const StageType type = setup.destination == DebugRunDestination::FinalBoss ? StageType::Boss :
        setup.destination == DebugRunDestination::MidBoss ? StageType::MidBoss : StageType::Normal;
    if (battle && !setup.stageId.empty())
    {
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        const auto found = std::find_if(stages.begin(), stages.end(), [&](const StageData& s) { return s.id == setup.stageId && s.stageType == type; });
        if (found == stages.end()) return false;
        selected = *found;
    }
    // Destroy the previous scene before applying the destination state. ChangeScene captures HP.
    game.ChangeScene(SceneType::Select);
    game.m_BossShotPlanner->Reset();
    game.m_StageOverrideState->ClearQueued();
    game.m_StageOverrideState->ClearCurrent();
    auto state = setup.BuildProgress(game.m_RunController->Progress().GetMap().Seed());
    // Jumping does not fabricate cleared battles or grant skipped rewards.
    state.clearedBattles = game.m_RunController->Progress().GetClearedBattleCount();
    state.phaseNormalBattleEncounters = game.m_RunController->Progress().GetPhaseNormalBattleEncounterCount();
    game.m_RunController->Progress().Restore(std::move(state));
    game.m_RunController->Status().progress = (std::min)(kNormalRouteAreaGoal, game.GetAreaProgress() + 1);
    game.m_PersistenceRuntime->Statistics().CompleteArea(game.GetAreaProgress());
    game.m_RunController->SetCombatMode(setup.combatMode);
    SetInfiniteMoney(game, setup.infiniteMoney);
    if (selected) game.m_StageOverrideState->Queue(*selected);
    if (battle) game.StartNextBattle(type);
    else if (setup.destination == DebugRunDestination::Shop) game.ChangeScene(SceneType::Shop);
    else if (setup.destination == DebugRunDestination::Rest || setup.destination == DebugRunDestination::Preparation) game.ChangeScene(SceneType::RestSite);
    else game.ChangeScene(SceneType::Select);
    game.RecordBalanceEvent("debug_run_jump", {{"area_progress", game.GetAreaProgress()}, {"stage_id", setup.stageId}, {"infinite_money", setup.infiniteMoney}});
    return true;
}

/// <summary>
/// デバッグランが進行中で、デバッグ用の移動操作を行えるかを返す。
/// </summary>
bool GameDebugHost::CanJumpRun(const Game& game)
{
    return game.IsDebugRunMode() && game.IsRunSessionActive();
}

/// <summary>
/// デバッグランの戦闘を即時勝利として処理し、その操作をログへ記録する。
/// </summary>
bool GameDebugHost::WinBattle(Game& game)
{
    if (!game.IsDebugRunMode() || game.GetCurrentSceneType() != SceneType::Battle || game.IsClearRewardActive()) return false;
    game.SetPaused(false);
    game.RecordBalanceEvent("debug_instant_victory", {{"stage_id", game.GetSelectedStageId()}});
    for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
        if (enemy && !enemy->IsDefeated()) enemy->Defeat();
    game.CompleteCurrentStage();
    return true;
}

/// <summary>
/// 現在のプレイヤー状態と乱数シードなどをデバッグ戦闘設定に取り込む。
/// </summary>
void GameDebugHost::CaptureSetup(Game& game, DebugBattleSetup& setup)
{
    game.CaptureCurrentPlayerStatus();
    const auto& status = game.m_RunController->Status();
    setup.seed = game.m_BalanceRuntime->CaptureReadSnapshot(
        status.progress, game.m_RunController->Progress().GetClearedBattleCount()).runRandomSeed;
    setup.maxHp = status.maxHp;
    setup.hp = (std::max)(1, status.currentHp);
    setup.money = status.money;
    setup.deck.clear();
    for (int i = 0; i < game.GetDeckBallCount(); ++i) setup.deck.push_back(*game.GetDeckBall(i));
    setup.relics = game.m_RunController->Relics();
}

/// <summary>
/// 現在のデバッグランの進行度と行き先を、再開用の設定に取り込む。
/// </summary>
bool GameDebugHost::CaptureRunEntry(const Game& game, DebugRunSetup& setup)
{
    if (!CanJumpRun(game) || game.IsClearRewardActive()) return false;
    setup.completedAreas = game.GetAreaProgress();
    setup.combatMode = game.GetRunCombatMode();
    setup.stageId.clear();
    switch (game.GetCurrentSceneType())
    {
    case SceneType::Select:
        setup.destination = game.IsFinalBossRoute() ? DebugRunDestination::FinalBoss : DebugRunDestination::Route;
        break;
    case SceneType::Shop: setup.destination = DebugRunDestination::Shop; break;
    case SceneType::RestSite:
        setup.destination = game.IsBossPreparation() ? DebugRunDestination::Preparation : DebugRunDestination::Rest;
        break;
    case SceneType::Battle:
    {
        const StageData* current = game.GetCurrentStageOverride();
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        if (!current) current = StageDataLoader::FindById(stages, game.GetSelectedStageId());
        if (!current) return false;
        setup.destination = game.GetRunPhase() == RunPhase::FinalBoss ? DebugRunDestination::FinalBoss :
            current->stageType == StageType::MidBoss ? DebugRunDestination::MidBoss : DebugRunDestination::Battle;
        setup.stageId = current->id;
        break;
    }
    default: return false;
    }
    return true;
}

/// <summary>
/// 選択画面へ戻って設定を再適用し、指定ステージでデバッグ戦闘をやり直す。
/// </summary>
void GameDebugHost::RestartRunBattle(Game& game, StageData stage)
{
    game.ChangeScene(SceneType::Select);
    game.ApplyDebugRunSettings();
    game.m_BossShotPlanner->Reset();
    game.m_StageOverrideState->Queue(std::move(stage));
    game.StartNextBattle();
}

/// <summary>
/// デバッグラン開始前に保存する進行プロファイルと自動プレイ・検証の設定を取得する。
/// </summary>
DebugRunSnapshot GameDebugHost::CaptureRunSnapshot(const Game& game)
{
    DebugRunSnapshot snapshot;
    snapshot.progressionProfile = game.m_PersistenceRuntime->Profile();
    snapshot.autoPlayEnabled = game.m_BalanceRuntime->IsAutoPlayEnabled();
    snapshot.validationEnabled = game.m_BalanceRuntime->IsValidationEnabled();
    return snapshot;
}

/// <summary>
/// デバッグ進行設定を反映し、自動プレイとバランス検証を無効にする。
/// </summary>
void GameDebugHost::PrepareSession(
    Game& game,
    const DebugBattleSetup& setup)
{
    setup.ApplyProgressionTo(game.m_PersistenceRuntime->Profile());
    game.m_BalanceRuntime->SetAutoPlayEnabled(false);
    game.m_BalanceRuntime->SetValidationEnabled(false);
}

/// <summary>
/// デバッグラン用の進行、球カタログ、デッキ、HP、所持金、レリックを適用する。
/// </summary>
void GameDebugHost::ApplyRunSettings(
    Game& game,
    const DebugBattleSetup& setup,
    const std::vector<PlayerBallData>& ballCatalog)
{
    setup.ApplyProgressionTo(game.m_PersistenceRuntime->Profile());
    game.m_PersistenceRuntime->ActiveAscension() = setup.selectedAscension;
    std::vector<PlayerBallData> unlockedCatalog;
    for (const PlayerBallData& ball : ballCatalog)
    {
        if (game.m_PersistenceRuntime->Profile().IsBallUnlocked(ball.definitionId))
        {
            unlockedCatalog.push_back(ball);
        }
    }
    game.m_RunController->Deck().SetCatalog(unlockedCatalog);
    game.m_RunController->Deck().SetDefaultDeck(setup.deck);
    game.m_RunController->Deck().ResetToDefault();
    game.m_RunController->Status().maxHp = setup.maxHp;
    game.m_RunController->Status().currentHp = std::clamp(
        setup.hp,
        1,
        game.m_RunController->Status().maxHp);
    game.m_RunController->Status().money = setup.money;
    game.m_RunController->RestHealRatio() = game.m_PersistenceRuntime->DefaultRestHealRatio();
    game.m_RunController->Relics() = setup.relics;
}

/// <summary>
/// 指定した乱数シードとステージを使い、単独のデバッグ戦闘を開始する。
/// </summary>
void GameDebugHost::StartBattle(
    Game& game,
    std::uint32_t seed,
    StageData stage)
{
    const StageType stageType = stage.stageType;
    game.m_BossShotPlanner->Reset();
    game.m_StageOverrideState->ClearQueued();
    game.StartNewRun("debug_sandbox", "manual_debug", "", "", seed);
    game.m_StageOverrideState->Queue(std::move(stage));
    game.StartNextBattle(stageType);
}

/// <summary>
/// デバッグセッションを終了し、開始前の進行プロファイルと自動プレイ・検証の設定を復元する。
/// </summary>
void GameDebugHost::EndSession(
    Game& game,
    const DebugRunSnapshot& snapshot)
{
    game.DeactivateRunSession();
	game.m_RunController->SetDebugInfiniteMoney(false);
	game.SetPaused(false);
    game.m_BalanceRuntime->SetAutoPlayEnabled(snapshot.autoPlayEnabled);
    game.m_BalanceRuntime->SetValidationEnabled(snapshot.validationEnabled);
    game.m_StageOverrideState->ClearQueued();
    game.m_BossShotPlanner->Reset();
    game.m_PersistenceRuntime->Profile() = snapshot.progressionProfile;
    game.m_PersistenceRuntime->ActiveAscension() = 0;
    game.LoadPlayerStatusFromJson();
}

/// <summary>
/// 盤面上のプレイヤー球から現在のプレイヤー状態を取り込む。
/// </summary>
void GameDebugHost::CaptureCurrentPlayerStatus(Game& game)
{
    game.CaptureCurrentPlayerStatus();
}

/// <summary>
/// デバッグ操作からゲームのポーズ状態を設定する。
/// </summary>
void GameDebugHost::SetPaused(Game& game, bool paused)
{
	game.SetPaused(paused);
}

/// <summary>
/// 外部操作との連携が初期化されていれば、連携側にゲームの更新を渡す。
/// </summary>
void GameDebugHost::UpdateExternalBridge(Game& game)
{
    if (game.m_GameMcpBridge != nullptr)
    {
        game.m_GameMcpBridge->Update(game);
    }
}

/// <summary>
/// 通常の休憩所で適用する既定の回復割合を取得する。
/// </summary>
float GameDebugHost::GetDefaultRestHealRatio(const Game& game)
{
    return game.m_PersistenceRuntime->DefaultRestHealRatio();
}

/// <summary>
/// 自動プレイとラン終了時の停止要求をデバッグ表示用の状態にまとめる。
/// </summary>
DebugAutomationSnapshot GameDebugHost::CaptureAutomation(const Game& game)
{
    DebugAutomationSnapshot snapshot;
    const GameBalanceAutomationSnapshot automation =
        game.m_BalanceRuntime->CaptureAutomationSnapshot();
    snapshot.enabled = automation.enabled;
    snapshot.stopAfterCurrentRunRequested =
        automation.stopAfterCurrentRunRequested;
    snapshot.stopAfterCurrentRunDefault =
        automation.stopAfterCurrentRunDefault;
    snapshot.runCount = automation.runCount;
    snapshot.maximumRuns = automation.maximumRuns;
    return snapshot;
}

/// <summary>
/// デバッグ操作から自動プレイの有効状態を設定する。
/// </summary>
void GameDebugHost::SetAutoPlayEnabled(Game& game, bool enabled)
{
    game.m_BalanceRuntime->SetAutoPlayEnabled(enabled);
}

/// <summary>
/// 現在のラン完了時に自動プレイを停止する要求を設定する。
/// </summary>
void GameDebugHost::SetStopAfterCurrentRunRequested(
    Game& game,
    bool requested)
{
    game.m_BalanceRuntime->SetStopAfterCurrentRunRequested(requested);
}

/// <summary>
/// 現在のデバッグ状態をスナップショットに保存する。
/// </summary>
void GameDebugHost::SaveSnapshot(Game& game)
{
    game.SaveDebugSnapshot();
}

/// <summary>
/// 必要な球・敵データなどを読み込み、デバッグエディターを開く。
/// </summary>
void GameDebugController::Open(Game& game)
{
    if (!m_DebugMode && !GameDebugHost::CanOpenEditor(game)) return;
    if (m_DebugBallCatalog.empty())
    {
        m_DebugBallCatalog = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {}).ballDefinitions;
        m_DebugEnemyCatalog = StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json");
        m_DebugStages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        m_DebugSetup.deck = PlayerBallDataLoader::LoadDeck("assets/data/player_deck.json", m_DebugBallCatalog);
        if (!m_DebugStages.empty())
            for (const auto& spawn : m_DebugStages.front().enemies)
                m_DebugSetup.enemies.push_back({spawn, spawn.enemyData.maxHp});
    }
    if (IsRunActive())
    {
        GameDebugHost::CaptureSetup(game, m_DebugSetup);
        const StageData* stage = game.GetCurrentStageOverride();
        if (!stage)
        {
            const auto found = std::find_if(m_DebugStages.begin(), m_DebugStages.end(), [&](const StageData& s) { return s.id == game.GetSelectedStageId(); });
            if (found != m_DebugStages.end()) stage = &*found;
        }
        if (stage && game.GetCurrentSceneType() == SceneType::Battle && !game.IsClearRewardActive())
        {
            m_StageEditor.Load("assets/data/stage_01.json", *stage);
            m_DebugUseStageLayout = true;
        }
    }
    m_DebugEditorOpen = true;
    RefreshRunPresets();
    m_DebugDeckDragCatalog = m_DebugDeckDragSlot = m_DebugEnemyDragCatalog = -1;
    // Recover old detached/off-screen editor positions when opening the panel.
    GameUi::windowRevisions["debug_mode_editor"] = GameUi::layoutRevision - 1;
    if (m_StageEditor.draft.is_null() && !m_DebugStages.empty())
    {
        try
        {
            m_StageEditor.Load("assets/data/stage_01.json", m_DebugStages.front());
            for (size_t i = 0; i < m_DebugEnemyCatalog.size(); ++i)
                if (m_DebugEnemyCatalog[i].id == "enemy_normal") m_StageEditor.palette = static_cast<int>(i);
        }
        catch (const std::exception& e) { m_DebugMessage = e.what(); }
    }
    Game::ResetFrameTiming();
}

/// <summary>
/// 現在編集中のデバッグ設定をランへ反映する。
/// </summary>
void GameDebugController::ApplyRunSettings(Game& game)
{
    GameDebugHost::ApplyRunSettings(game, m_DebugSetup, m_DebugBallCatalog);
}

/// <summary>
/// デバッグ設定と配置を検証し、指定条件で単独の戦闘を開始する。
/// </summary>
bool GameDebugController::StartBattle(Game& game)
{
    m_DebugMessage = m_DebugSetup.Validate();
    if (!m_DebugMessage.empty()) return false;
    StageData stage;
    stage.id = "debug_battle";
    if (m_DebugStageOverride.has_value())
    {
        stage = *m_DebugStageOverride;
    }
    else if (m_DebugSetup.enemyValidation.enabled)
    {
        EnemyValidationSettings activeValidation =
            m_DebugSetup.enemyValidation;
        activeValidation.seed = m_DebugSetup.seed;
        float playerRadius = 0.01f;
        for (const PlayerBallData& ball : m_DebugSetup.deck)
        {
            playerRadius = (std::max)(playerRadius, ball.status.radius);
        }
        EnemyValidationLayoutResult generated =
            EnemyValidationLayout::Generate(
                m_DebugEnemyCatalog,
                activeValidation,
                m_DebugSetup.playerPosition,
                playerRadius);
        if (!generated.Succeeded())
        {
            m_DebugMessage = generated.error;
            return false;
        }
        stage.enemies = std::move(generated.enemies);
        stage.preserveLayout = true;
        stage.enemyValidation = activeValidation;
    }
    else
    {
        for (const auto& enemy : m_DebugSetup.enemies)
        {
            stage.enemies.push_back(enemy.spawn);
            // 編集した半径を見た目にも反映し、当たり判定だけが大きくならないようにする。
            const float radius = enemy.spawn.enemyData.status.radius;
            stage.enemies.back().enemyData.scale = {radius, radius, radius};
            if (enemy.spawn.enemyData.id == "enemy_boss_core") stage.stageType = StageType::Boss;
            else if (stage.stageType != StageType::Boss && enemy.spawn.enemyData.id.find("midboss") != std::string::npos)
                stage.stageType = StageType::MidBoss;
        }
    }
    m_DebugStageOverride.reset();
    m_DebugActiveStage = stage;
    // 描画中にSceneを破棄しない。再戦でも前の球を破棄してから初期状態を作る。
    if (m_DebugMode) game.ChangeScene(SceneType::Title);
    m_DebugRunActive = false;
    m_DebugPreviousRun = GameDebugHost::CaptureRunSnapshot(game);
    GameDebugHost::PrepareSession(game, m_DebugSetup);
    m_DebugMode = true;
    m_DebugActiveSetup = m_DebugSetup;
    m_DebugEditorOpen = m_DebugBattleFinished = false;
    GameDebugHost::StartBattle(game, m_DebugSetup.seed, std::move(stage));
    for (auto* player : GameRuntime::World().GetComponents<PlayerBall>()) player->SetState(PlayerBall::State::Idle);
    BalanceLogger::GetInstance().RecordEvent("debug_battle_setup", m_DebugSetup.ToJson());
    m_DebugMessage = "設定した条件で戦闘を開始しました。";
    Game::ResetFrameTiming();
    return true;
}

/// <summary>
/// デバッグ設定を検証して現在の設定を退避し、指定条件のランを開始する。
/// </summary>
bool GameDebugController::StartRun(Game& game)
{
    m_DebugMessage = m_DebugSetup.Validate();
    if (!m_DebugMessage.empty()) return false;
    if (m_DebugMode) game.ChangeScene(SceneType::Title);
    m_DebugPreviousRun = GameDebugHost::CaptureRunSnapshot(game);
    GameDebugHost::PrepareSession(game, m_DebugSetup);
    m_DebugMode = m_DebugRunActive = true;
    m_DebugRunInfiniteMoneyActive = m_DebugRunSetup.infiniteMoney;
    GameUi::windowRevisions["debug_run_controls"] = GameUi::layoutRevision - 1;
    m_DebugEditorOpen = m_DebugBattleFinished = false;
    m_DebugActiveSetup = m_DebugSetup;
    m_DebugStageOverride.reset();
    GameDebugHost::StartRun(game, m_DebugRunSetup, m_DebugSetup.seed);
    if (!GameDebugHost::JumpRun(game, m_DebugRunSetup))
    {
        m_DebugEditorOpen = true;
        m_DebugMessage = "移動先のステージを読み込めません。自動選択か別のステージを選んでください。";
        return false;
    }
    Game::ResetFrameTiming();
    return true;
}

/// <summary>
/// 直前に実行した配置と設定を復元し、デバッグ戦闘の再開を要求する。
/// </summary>
void GameDebugController::RequestReplay()
{
    m_DebugSetup = m_DebugActiveSetup;
    m_DebugStageOverride = m_DebugActiveStage;
    m_DebugRequest = 1;
}

/// <summary>
/// 単独のデバッグ戦闘でプレイヤー球を指定の初期位置へ移す。
/// </summary>
void GameDebugController::ApplyBattlePlayer(PlayerBall* player)
{
    if (!m_DebugMode || m_DebugRunActive || !player) return;
    player->GetBall()->ResetAtPosition(m_DebugSetup.playerPosition);
    player->GetBall()->SetInitialPosition(m_DebugSetup.playerPosition);
}

/// <summary>
/// 単独のデバッグ戦闘で対象の敵球に指定の位置と能力値を適用する。
/// </summary>
void GameDebugController::ApplyBattleEnemy(EnemyBall* enemy, std::size_t index)
{
    if (!m_DebugMode || m_DebugRunActive || !enemy) return;
    if (m_DebugActiveSetup.enemyValidation.enabled)
    {
        enemy->SetHP(enemy->GetMaxHP());
        return;
    }
    if (index >= m_DebugActiveSetup.enemies.size()) return;
    const DebugBattleSetup::Enemy& configured =
        m_DebugActiveSetup.enemies[index];
    const int configuredMaxHp = (std::max)(1, configured.spawn.enemyData.maxHp);
    const float hpRatio = std::clamp(
        static_cast<float>(configured.hp) / static_cast<float>(configuredMaxHp),
        0.0f,
        1.0f);
    const int scaledHp = std::clamp(
        static_cast<int>(std::lround(enemy->GetMaxHP() * hpRatio)),
        1,
        enemy->GetMaxHP());
    enemy->SetHP(scaledHp);
    enemy->SetDebugBossState(m_DebugSetup.armor, m_DebugSetup.breakShots);
}

/// <summary>
/// デバッグの記録を終了し、デバッグセッションと画面の状態をリセットする。
/// </summary>
void GameDebugController::End(Game& game)
{
    if (!m_DebugMode) return;
    BalanceLogger::GetInstance().EndRun(
        "debug_closed",
        game.GetPlayerCurrentHp(),
        game.GetPlayerMaxHp(),
        0);
    m_DebugMode = m_DebugEditorOpen = m_DebugBattleFinished = false;
    m_DebugRunActive = false;
    if (m_DebugPreviousRun.has_value())
    {
        GameDebugHost::EndSession(game, *m_DebugPreviousRun);
        m_DebugPreviousRun.reset();
    }
}

/// <summary>
/// デバッグ戦闘の勝敗と終了時の数値を記録し、戦闘を終了状態にする。
/// </summary>
void GameDebugController::FinishBattle(Game& game, bool victory)
{
    if (m_DebugBattleFinished) return;
    GameDebugHost::CaptureCurrentPlayerStatus(game);
    const auto enemies = GameRuntime::World().GetComponents<EnemyBall>();
    const int defeated = static_cast<int>(std::count_if(enemies.begin(), enemies.end(), [](auto* e) { return e->IsDefeated(); }));
    auto& logger = BalanceLogger::GetInstance();
    const int currentHp = game.GetPlayerCurrentHp();
    const int maximumHp = game.GetPlayerMaxHp();
    logger.EndShot(currentHp, static_cast<int>(enemies.size()) - defeated, defeated);
    logger.EndStage(victory ? "clear" : "game_over", currentHp, maximumHp, defeated);
    logger.EndRun(victory ? "debug_clear" : "debug_game_over", currentHp, maximumHp, victory ? 1 : 0);
    m_DebugBattleFinished = m_DebugEditorOpen = true;
    GameDebugHost::SetPaused(game, false);
    m_DebugMessage = victory ? "戦闘クリア。条件を変えるか、同じ条件で再戦できます。" : "戦闘終了（HP 0）。条件を変えるか、同じ条件で再戦できます。";
    Game::ResetFrameTiming();
}

/// <summary>
/// デバッグのキー入力、開始・終了要求、エディターの状態をフレームごとに処理する。
/// </summary>
bool GameDebugController::Update(Game& game)
{
    // Handle this before the editor's early return so it also works while paused.
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false) && (m_DebugMode || GameDebugHost::CanOpenEditor(game)))
    {
        if (m_DebugEditorOpen && m_DebugMode && !m_DebugBattleFinished)
        {
            m_DebugEditorOpen = false;
            GameDebugHost::SetPaused(game, false);
        }
        else Open(game);
    }
    const int request = std::exchange(m_DebugRequest, 0);
    if (request == 3) { StartRun(game); return true; }
    if (request == 4)
    {
        if (GameDebugHost::JumpRun(game, m_DebugRunSetup))
        {
            m_DebugEditorOpen = false;
            m_DebugRunInfiniteMoneyActive = m_DebugRunSetup.infiniteMoney;
        }
        else m_DebugMessage = "指定先へ移動できませんでした。ステージ種別とランの状態を確認してください。";
        Game::ResetFrameTiming(); return true;
    }
    if (request == 5)
    {
        m_DebugMessage = m_DebugSetup.Validate();
        if (IsRunActive() && m_DebugStageOverride && m_DebugMessage.empty() &&
            game.GetCurrentSceneType() == SceneType::Battle && !game.IsClearRewardActive())
        {
            m_DebugEditorOpen = false;
            m_DebugRunInfiniteMoneyActive = m_DebugRunSetup.infiniteMoney;
            GameDebugHost::RestartRunBattle(game, *m_DebugStageOverride);
            m_DebugStageOverride.reset();
        }
        Game::ResetFrameTiming(); return true;
    }
    if (request == 6) { GameDebugHost::WinBattle(game); Game::ResetFrameTiming(); return true; }
    if (IsRunActive()) GameDebugHost::SetInfiniteMoney(game, m_DebugRunInfiniteMoneyActive);
    if (request == 1) { StartBattle(game); Game::ResetFrameTiming(); return true; }
    if (request == 2)
    {
        m_DebugEditorOpen = false;
        if (m_DebugMode) game.ChangeScene(SceneType::Title);
        Game::ResetFrameTiming(); return true;
    }
    if (m_DebugEditorOpen)
    {
        // 編集中も状態を公開するが、外部からの戦闘操作はBridge側で拒否
        GameDebugHost::UpdateExternalBridge(game);
        Game::ResetFrameTiming(); return true;
    }
    return false;
}

/// <summary>
/// 編集中のデバッグ戦闘設定を検証し、一時ファイルを経由してプリセットに保存する。
/// </summary>
void GameDebugController::SavePreset()
{
    m_DebugMessage = m_DebugSetup.Validate();
    if (!m_DebugMessage.empty()) return;
    try
    {
        std::filesystem::create_directories(PresetPath.parent_path());
        const auto temporary = PresetPath.string() + ".tmp";
        { std::ofstream file(temporary); file << m_DebugSetup.ToJson().dump(2); file.close(); if (!file) throw std::runtime_error("書き込みに失敗しました。"); }
        if (!MoveFileExA(temporary.c_str(), PresetPath.string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("保存ファイルの更新に失敗しました。");
        m_DebugMessage = "デバッグ条件を保存しました（通常セーブとは別ファイル）。";
    }
    catch (const std::exception& e) { m_DebugMessage = std::string("保存できませんでした：") + e.what(); }
}

/// <summary>
/// 保存済みのデバッグ戦闘プリセットを読み込んで検証し、編集中の設定へ反映する。
/// </summary>
bool GameDebugController::LoadPreset()
{
    try
    {
        if (std::filesystem::file_size(PresetPath) > 1024 * 1024) throw std::runtime_error("ファイルが大きすぎます。");
        std::ifstream file(PresetPath); nlohmann::json value; file >> value;
        auto next = DebugBattleSetup::FromJson(value, m_DebugBallCatalog, m_DebugEnemyCatalog);
        m_DebugSetup = std::move(next); // 検証に失敗したときは編集中の条件を維持
        m_DebugStageOverride.reset();
        m_DebugUseStageLayout = false;
        m_DebugMessage = "デバッグ条件を読み込みました。「この条件で戦闘開始」で適用します。";
        return true;
    }
    catch (const std::exception& e) { m_DebugMessage = std::string("読み込めませんでした：") + e.what(); }
    return false;
}

/// <summary>
/// 保存済みのランプリセット一覧を更新し、一覧から消えた選択を整理する。
/// </summary>
void GameDebugController::RefreshRunPresets()
{
    try
    {
        m_DebugRunPresetNames = DebugRunPresetStore::List(RunPresetsPath);
        const bool found = std::find(m_DebugRunPresetNames.begin(), m_DebugRunPresetNames.end(), m_DebugRunPresetSelected) != m_DebugRunPresetNames.end();
        SelectRunPreset(found ? m_DebugRunPresetSelected : m_DebugRunPresetNames.empty() ? "" : m_DebugRunPresetNames.front());
    }
    catch (const std::exception& e)
    {
        m_DebugRunPresetNames.clear();
        SelectRunPreset("");
        m_DebugRunPresetMessage = std::string("一覧を取得できませんでした：") + e.what();
    }
    RefreshRunPresetComparison();
}

/// <summary>
/// 指定した保存済みランプリセットを選択し、プレビュー用に読み込む。
/// </summary>
void GameDebugController::SelectRunPreset(const std::string& name)
{
    // This only updates the saved-record preview, never the live/draft run setup.
    m_DebugRunPresetSelected = name;
    m_DebugRunPresetPreview.reset();
    m_DebugRunPresetPreviewError.clear();
    snprintf(m_DebugRunPresetRename, sizeof(m_DebugRunPresetRename), "%s", name.c_str());
    m_DebugRunPresetCopyName[0] = '\0';
    if (name.empty()) return;
    const std::string copyBase = name.size() <= 112 ? name : "comparison";
    std::string copyName = copyBase + "_copy";
    int suffix = 2;
    while (std::find(m_DebugRunPresetNames.begin(), m_DebugRunPresetNames.end(), copyName) != m_DebugRunPresetNames.end())
        copyName = copyBase + "_copy" + std::to_string(suffix++);
    snprintf(m_DebugRunPresetCopyName, sizeof(m_DebugRunPresetCopyName), "%s", copyName.c_str());
    snprintf(m_DebugRunPresetName, sizeof(m_DebugRunPresetName), "%s", name.c_str());
    try
    {
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        m_DebugRunPresetPreview = DebugRunPreset::FromJson(DebugRunPresetStore::Load(RunPresetsPath, name),
            m_DebugBallCatalog, m_DebugEnemyCatalog, stages);
    }
    catch (const std::exception& e) { m_DebugRunPresetPreviewError = e.what(); }
}

/// <summary>
/// 選択中のランプリセットを改名し、選択と比較の参照先も更新する。
/// </summary>
void GameDebugController::RenameRunPreset()
{
    try
    {
        const std::string newName = m_DebugRunPresetRename;
        DebugRunPresetStore::Rename(RunPresetsPath, m_DebugRunPresetSelected, newName);
        for (auto& name : m_DebugRunPresetCompareNames)
            if (name == m_DebugRunPresetSelected) name = newName;
        m_DebugRunPresetSelected = newName;
        RefreshRunPresets();
        m_DebugRunPresetMessage = "名前を変更しました：" + newName;
    }
    catch (const std::exception& e) { m_DebugRunPresetMessage = std::string("名前を変更できませんでした：") + e.what(); }
}

/// <summary>
/// 選択中のランプリセットを複製し、比較先を設定して必要に応じて複製を編集する。
/// </summary>
void GameDebugController::DuplicateRunPreset(bool editCopy)
{
    try
    {
        const std::string newName = m_DebugRunPresetCopyName;
        DebugRunPresetStore::Duplicate(RunPresetsPath, m_DebugRunPresetSelected, newName);
        m_DebugRunPresetCompareNames[0] = m_DebugRunPresetSelected;
        m_DebugRunPresetCompareNames[1] = newName;
        m_DebugRunPresetSelected = newName;
        RefreshRunPresets();
        m_DebugRunPresetMessage = "別名で複製しました：" + newName;
        if (editCopy)
        {
            if (LoadRunPreset(false)) m_DebugRunPresetMessage = "複製先を編集中：" + newName + "。デッキ・レリックを変更して開始設定を保存してください。";
            else m_DebugRunPresetMessage = "複製先は保存済みです。" + m_DebugRunPresetMessage;
        }
    }
    catch (const std::exception& e) { m_DebugRunPresetMessage = std::string("複製できませんでした：") + e.what(); }
}

/// <summary>
/// 指定した保存済みランプリセットを削除し、選択と一覧を更新する。
/// </summary>
void GameDebugController::DeleteRunPreset()
{
    try
    {
        const auto name = m_DebugRunPresetDeleteTarget;
        DebugRunPresetStore::Delete(RunPresetsPath, name);
        if (m_DebugRunPresetSelected == name) m_DebugRunPresetSelected.clear();
        RefreshRunPresets();
        m_DebugRunPresetMessage = "削除しました：" + name;
    }
    catch (const std::exception& e) { m_DebugRunPresetMessage = std::string("削除できませんでした：") + e.what(); }
}

/// <summary>
/// 編集中の設定、または進行中のデバッグランの状態をランプリセットに保存する。
/// </summary>
void GameDebugController::SaveRunPreset(Game& game, bool currentRun)
{
    try
    {
        DebugRunPreset preset{m_DebugRunSetup, m_DebugSetup};
        if (currentRun)
        {
            if (!GameDebugHost::CaptureRunEntry(game, preset.run))
                throw std::runtime_error("報酬画面・リザルト以外の、デバッグラン進行中に保存してください。");
            preset.battle = m_DebugActiveSetup;
            GameDebugHost::CaptureSetup(game, preset.battle);
            preset.run.infiniteMoney = m_DebugRunInfiniteMoneyActive;
        }
        // Validate against today's catalogs before touching the existing file.
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        const auto value = preset.ToJson();
        DebugRunPreset::FromJson(value, m_DebugBallCatalog, m_DebugEnemyCatalog, stages);
        DebugRunPresetStore::Save(RunPresetsPath, m_DebugRunPresetName, value);
        m_DebugRunPresetSelected = m_DebugRunPresetName;
        RefreshRunPresets();
        m_DebugRunPresetMessage = std::string("保存しました：") + m_DebugRunPresetName;
    }
    catch (const std::exception& e) { m_DebugRunPresetMessage = std::string("保存できませんでした：") + e.what(); }
}

/// <summary>
/// 選択したランプリセットを検証して読み込み、指定に応じてその条件での開始を要求する。
/// </summary>
bool GameDebugController::LoadRunPreset(bool startRun)
{
    try
    {
        if (m_DebugRunPresetSelected.empty()) throw std::runtime_error("保存済みプリセットを選択してください。");
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        auto next = DebugRunPreset::FromJson(DebugRunPresetStore::Load(RunPresetsPath, m_DebugRunPresetSelected),
            m_DebugBallCatalog, m_DebugEnemyCatalog, stages);
        m_DebugSetup = std::move(next.battle);
        m_DebugRunSetup = std::move(next.run);
        m_DebugStageOverride.reset();
        m_DebugRunPresetMessage = std::string("読み込みました：") + m_DebugRunPresetSelected + "（ラン開始で適用）";
        // Do not call Open(): it would overwrite the loaded deck with the live run.
        m_DebugEditorOpen = true;
        m_DebugSelectRunTab = true;
        GameUi::windowRevisions["debug_mode_editor"] = GameUi::layoutRevision - 1;
        if (startRun) m_DebugRequest = 3;
        Game::ResetFrameTiming();
        return true;
    }
    catch (const std::exception& e) { m_DebugRunPresetMessage = std::string("読み込めませんでした：") + e.what(); }
    return false;
}

/// <summary>
/// 選択した2つの保存済みランプリセットを読み込み、表示用の差分を計算する。
/// </summary>
void GameDebugController::RefreshRunPresetComparison()
{
    m_DebugRunPresetCompareRows.clear();
    m_DebugRunPresetCompareError.clear();
    if (m_DebugRunPresetCompareNames[0].empty() || m_DebugRunPresetCompareNames[1].empty()) return;
    try
    {
        const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        DebugRunPreset presets[2];
        for (int i = 0; i < 2; ++i)
        {
            try
            {
                presets[i] = DebugRunPreset::FromJson(DebugRunPresetStore::Load(RunPresetsPath, m_DebugRunPresetCompareNames[i]),
                    m_DebugBallCatalog, m_DebugEnemyCatalog, stages);
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(std::string(i == 0 ? "比較元「" : "比較先「") + m_DebugRunPresetCompareNames[i] + "」：" + e.what());
            }
        }
        m_DebugRunPresetCompareRows = CompareDebugRunPresets(presets[0], presets[1]);
    }
    catch (const std::exception& e) { m_DebugRunPresetCompareError = e.what(); }
}

/// <summary>
/// 2つの保存済みランプリセットを選択し、差分を確認する比較画面を描画する。
/// </summary>
void GameDebugController::DrawRunPresetComparison()
{
    if (!m_DebugRunPresetCompareOpen) return;
    ImGui::SetNextWindowSize(ImVec2(960, 650), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ランプリセットの差分比較", &m_DebugRunPresetCompareOpen))
    {
        for (int i = 0; i < 2; ++i)
        {
            ImGui::SetNextItemWidth(360);
            const auto& selected = m_DebugRunPresetCompareNames[i];
            if (ImGui::BeginCombo(i == 0 ? "比較元" : "比較先", selected.empty() ? "保存済みプリセットを選択" : selected.c_str()))
            {
                for (const auto& name : m_DebugRunPresetNames)
                    if (ImGui::Selectable(name.c_str(), selected == name))
                    {
                        m_DebugRunPresetCompareNames[i] = name;
                        RefreshRunPresetComparison();
                    }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("比較元と比較先を入れ替え"))
        {
            std::swap(m_DebugRunPresetCompareNames[0], m_DebugRunPresetCompareNames[1]);
            RefreshRunPresetComparison();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存内容を再取得")) RefreshRunPresets();
        ImGui::Checkbox("変更点のみ表示", &m_DebugRunPresetCompareChangesOnly);
        ImGui::TextWrapped("保存内容を比較します。デッキは保存順で比較し、追加・削除・並べ替えも表示します。比較操作では設定を適用しません。");
        if (!m_DebugRunPresetCompareError.empty())
            ImGui::TextWrapped("比較できません：%s", m_DebugRunPresetCompareError.c_str());
        else if (m_DebugRunPresetCompareRows.empty())
            ImGui::TextWrapped("比較する保存済みプリセットを2つ選択してください。");
        else
        {
            const auto changed = std::count_if(m_DebugRunPresetCompareRows.begin(), m_DebugRunPresetCompareRows.end(),
                [](const auto& row) { return row.changed; });
            ImGui::TextUnformatted(changed == 0 ? "設定差分はありません。" : "変更した行を黄色で表示します（性能の詳細行を含みます）。");
            if (ImGui::BeginTable("run_preset_diff", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                ImVec2(0, ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("比較元", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("比較先", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
                for (const auto& row : m_DebugRunPresetCompareRows)
                {
                    if (m_DebugRunPresetCompareChangesOnly && !row.changed) continue;
                    ImGui::TableNextRow();
                    if (row.changed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .85f, .35f, 1));
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", row.label.c_str());
                    for (const auto* text : {&row.before, &row.after})
                    {
                        const std::string value = row.key == "stage" ? RunStageLabel(m_DebugStages, *text) : *text;
                        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", value.c_str());
                    }
                    if (row.changed) ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

/// <summary>
/// 選択中の保存済みランプリセットの内容を、読み込み前のプレビューとして描画する。
/// </summary>
void GameDebugController::DrawRunPresetPreview()
{
    if (m_DebugRunPresetSelected.empty())
    {
        ImGui::TextDisabled("保存済みプリセットを選ぶと設定内容を表示します。");
        return;
    }
    if (!ImGui::CollapsingHeader("選択した保存内容（読み込み前の確認）", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!m_DebugRunPresetPreview)
    {
        ImGui::TextWrapped("内容を表示できません：%s", m_DebugRunPresetPreviewError.c_str());
        return;
    }
    const auto& preset = *m_DebugRunPresetPreview;
    const char* destinations[] = {"ルート選択", "通常戦", "中ボス戦", "ショップ", "休憩所", "最終ボス前の休憩", "最終ボス戦"};
    const bool finalArea = preset.run.destination == DebugRunDestination::Preparation || preset.run.destination == DebugRunDestination::FinalBoss;
    if (ImGui::BeginTable("saved_run_conditions", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("保存されている設定");
        ImGui::TableHeadersRow();
        auto row = [](const char* label, const std::string& value)
        {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", label);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", value.c_str());
        };
        row("プリセット", m_DebugRunPresetSelected);
        row("進行度", std::to_string(finalArea ? kNormalRouteAreaGoal : preset.run.completedAreas) + " / " + std::to_string(kNormalRouteAreaGoal));
        row("移動先", destinations[static_cast<int>(preset.run.destination)]);
        row("ステージ", RunStageLabel(m_DebugStages, preset.run.stageId));
        row("抽選ルール", preset.run.combatMode == RunCombatMode::PhaseTest ? "フェーズ制試遊" : "本編");
        row("ゴールド無限", preset.run.infiniteMoney ? "ON（開始時999999）" : "OFF");
        row("保存所持金", std::to_string(preset.battle.money));
        row("乱数シード", std::to_string(preset.battle.seed));
        row("HP / 基礎最大HP", std::to_string(preset.battle.hp) + " / " + std::to_string(preset.battle.maxHp));
        row("アセンション", "A" + std::to_string(preset.battle.selectedAscension));
        ImGui::EndTable();
    }
    ImGui::Text("デッキ：%d個（保存された順序）", static_cast<int>(preset.battle.deck.size()));
    if (ImGui::BeginTable("saved_run_deck", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 160)))
    {
        ImGui::TableSetupColumn("球"); ImGui::TableSetupColumn("強化");
        ImGui::TableSetupColumn("攻撃"); ImGui::TableSetupColumn("半径");
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        for (size_t i = 0; i < preset.battle.deck.size(); ++i)
        {
            const auto& ball = preset.battle.deck[i];
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("#%d %s", static_cast<int>(i + 1), BallLabel(ball.definitionId));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nID: %s\n質量 %.2f / 摩擦 %.3f / 反発 %.3f", BallLabel(ball.definitionId), ball.definitionId.c_str(), ball.status.mass, ball.status.friction, ball.status.restitution);
            ImGui::TableNextColumn(); ImGui::Text("+%d", ball.upgradeLevel);
            ImGui::TableNextColumn(); ImGui::Text("%d", ball.status.attack);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", ball.status.radius);
        }
        ImGui::EndTable();
    }
    const int relicCount = static_cast<int>(std::count(preset.battle.relics.begin(), preset.battle.relics.end(), true));
    ImGui::Text("レリック：%d個", relicCount);
    for (const auto& relic : RelicCatalog)
        if (preset.battle.relics[static_cast<size_t>(relic.type)]) ImGui::BulletText("%s", relic.name);
    if (relicCount == 0) ImGui::TextDisabled("なし");
    ImGui::TextDisabled("選択だけでは編集中の条件や現在のランへ反映しません。");
}

/// <summary>
/// 保存済みランプリセットの選択、比較、編集、保存を操作する画面を描画する。
/// </summary>
void GameDebugController::DrawRunPresets(Game& game)
{
    if (ImGui::Button("保存済みプリセットを2つ比較"))
    {
        if (m_DebugRunPresetCompareNames[0].empty()) m_DebugRunPresetCompareNames[0] = m_DebugRunPresetSelected;
        if (m_DebugRunPresetCompareNames[1].empty())
            for (const auto& name : m_DebugRunPresetNames)
                if (name != m_DebugRunPresetCompareNames[0]) { m_DebugRunPresetCompareNames[1] = name; break; }
        m_DebugRunPresetCompareOpen = true;
        RefreshRunPresetComparison();
    }
    ImGui::SeparatorText("ランプリセット");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("保存する名前", m_DebugRunPresetName, sizeof(m_DebugRunPresetName));
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("保存済み", m_DebugRunPresetSelected.empty() ? "選択してください" : m_DebugRunPresetSelected.c_str()))
    {
        for (const auto& name : m_DebugRunPresetNames)
            if (ImGui::Selectable(name.c_str(), name == m_DebugRunPresetSelected)) SelectRunPreset(name);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("一覧を更新")) RefreshRunPresets();
    const bool exists = std::find(m_DebugRunPresetNames.begin(), m_DebugRunPresetNames.end(), m_DebugRunPresetName) != m_DebugRunPresetNames.end();
    if (ImGui::Button(exists ? "開始設定を同名に上書き" : "開始設定を保存")) SaveRunPreset(game, false);
    if (IsRunActive())
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(!GameDebugHost::CanJumpRun(game) || game.IsClearRewardActive());
        if (ImGui::Button("現在のラン条件を保存")) SaveRunPreset(game, true);
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(m_DebugRunPresetSelected.empty());
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("複製先の名前", m_DebugRunPresetCopyName, sizeof(m_DebugRunPresetCopyName));
    if (ImGui::Button("別名で複製")) DuplicateRunPreset(false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_DebugRunPresetPreview.has_value());
    if (ImGui::Button("複製して編集")) DuplicateRunPreset(true);
    ImGui::EndDisabled();
    ImGui::TextWrapped("保存済みの条件をそのまま複製します。複製先でデッキ・レリックを変更して保存すると、元を残して比較できます。");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("変更後の名前", m_DebugRunPresetRename, sizeof(m_DebugRunPresetRename));
    if (ImGui::Button("選択したプリセットの名前を変更")) RenameRunPreset();
    ImGui::SameLine();
    if (ImGui::Button("選択したプリセットを削除"))
    {
        m_DebugRunPresetDeleteTarget = m_DebugRunPresetSelected;
        ImGui::OpenPopup("ランプリセットの削除確認");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("ランプリセットの削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("「%s」を削除しますか？", m_DebugRunPresetDeleteTarget.c_str());
        ImGui::TextUnformatted("編集中の条件と進行中のランは維持します。");
        if (ImGui::Button("削除する")) { DeleteRunPreset(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    DrawRunPresetPreview();
    ImGui::BeginDisabled(!m_DebugRunPresetPreview.has_value());
    if (ImGui::Button("選択したプリセットを読み込む")) LoadRunPreset(false);
    ImGui::SameLine();
    if (ImGui::Button("読み込んでラン開始")) LoadRunPreset(true);
    ImGui::EndDisabled();
    ImGui::TextWrapped("進行度・移動先・ステージID・デッキ・レリック・シード・HP・ゴールド設定を保存します。同名は上書き。盤面の途中状態は含みません。");
    if (!m_DebugRunPresetMessage.empty()) ImGui::TextWrapped("%s", m_DebugRunPresetMessage.c_str());
}

/// <summary>
/// デバッグランでの進行先、戦闘、勝利判定などを操作する画面を描画する。
/// </summary>
void GameDebugController::DrawRunTools(Game& game)
{
    ImGui::TextWrapped("デッキ・HP・レリックは開始時の条件タブを使用します。ラン中の移動は現在の所持品を維持し、飛ばしたエリアの報酬は付与しません。");
    int destination = static_cast<int>(m_DebugRunSetup.destination);
    const char* destinations[] = {"ルート選択", "通常戦", "中ボス戦", "ショップ", "休憩所", "最終ボス前の休憩", "最終ボス戦"};
    if (ImGui::Combo("移動先", &destination, destinations, IM_ARRAYSIZE(destinations)))
    {
        m_DebugRunSetup.destination = static_cast<DebugRunDestination>(destination);
        m_DebugRunSetup.stageId.clear();
    }
    const bool endRoute = destination >= static_cast<int>(DebugRunDestination::Preparation);
    ImGui::BeginDisabled(endRoute);
    ImGui::SliderInt("完了エリア数", &m_DebugRunSetup.completedAreas, 0, kNormalRouteAreaGoal - 1);
    ImGui::EndDisabled();
    ImGui::Text("移動先の進行度 %d / %d", endRoute ? kNormalRouteAreaGoal : m_DebugRunSetup.completedAreas, kNormalRouteAreaGoal);
    int mode = m_DebugRunSetup.combatMode == RunCombatMode::PhaseTest ? 1 : 0;
    const char* modes[] = {"本編のステージ抽選", "フェーズ制試遊の抽選"};
    if (ImGui::Combo("自動選択ルール", &mode, modes, 2))
        m_DebugRunSetup.combatMode = mode ? RunCombatMode::PhaseTest : RunCombatMode::Legacy;
    const bool battle = destination == 1 || destination == 2 || destination == 6;
    if (battle)
    {
        const StageType type = destination == 6 ? StageType::Boss : destination == 2 ? StageType::MidBoss : StageType::Normal;
        const auto preview = RunStageLabel(m_DebugStages, m_DebugRunSetup.stageId);
        if (ImGui::BeginCombo("今回のステージ", preview.c_str()))
        {
            if (ImGui::Selectable("進行度から自動選択", m_DebugRunSetup.stageId.empty())) m_DebugRunSetup.stageId.clear();
            for (const auto& stage : m_DebugStages)
            {
                if (stage.stageType != type) continue;
                const auto label = stage.DisplayName() + "###" + stage.id;
                if (ImGui::Selectable(label.c_str(), stage.id == m_DebugRunSetup.stageId))
                    m_DebugRunSetup.stageId = stage.id;
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::Checkbox("ゴールド無限（支払いで減らない）", &m_DebugRunSetup.infiniteMoney) && IsRunActive() && !m_DebugEditorOpen)
        m_DebugRunInfiniteMoneyActive = m_DebugRunSetup.infiniteMoney;
    if (IsRunActive())
    {
        ImGui::Text("現在の進行度 %d / %d / 所持金 %d（無限 %s）", game.GetAreaProgress(), kNormalRouteAreaGoal, game.GetPlayerMoney(), m_DebugRunInfiniteMoneyActive ? "ON" : "OFF");
        ImGui::BeginDisabled(!GameDebugHost::CanJumpRun(game));
        if (ImGui::Button("現在のデッキで指定先へ移動")) m_DebugRequest = 4;
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button(IsRunActive() ? "新しいデバッグランを開始" : "この条件でデバッグランを開始")) m_DebugRequest = 3;
    ImGui::TextDisabled("通常ランのセーブ・実績には反映しません。F3で条件編集／再開。");
    DrawRunPresets(game);
}

/// <summary>
/// デバッグモードの状態に応じた操作画面と、デバッグ設定を編集する画面を描画する。
/// </summary>
void GameDebugController::Draw(Game& game)
{
    if (m_DebugEditorOpen || m_DebugMode) DrawRunPresetComparison();
    if (m_DebugMode && !m_DebugEditorOpen)
    {
        GameUi::PrepareWindow(IsRunActive() ? "debug_run_controls" : "debug_mode_controls", ImVec2(600, 20), IsRunActive() ? ImVec2(590, 620) : ImVec2(450, 130));
        ImGui::Begin(IsRunActive() ? "デバッグラン操作" : "デバッグ戦闘", nullptr, IsRunActive() ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoCollapse);
        if (IsRunActive())
        {
            ImGui::TextColored(ImVec4(1, .8f, .2f, 1), "デバッグラン進行中");
            DrawRunTools(game);
            ImGui::BeginDisabled(game.GetCurrentSceneType() != SceneType::Battle || game.IsClearRewardActive());
            if (ImGui::Button("即時勝利して報酬へ")) m_DebugRequest = 6;
            ImGui::EndDisabled();
        }
        ImGui::TextUnformatted("通常セーブ・通常ランの集計から独立しています。");
        if (ImGui::Button("配置・デッキを編集（F3）")) Open(game);
        if (!IsRunActive())
        {
            ImGui::SameLine();
            if (ImGui::Button("同じ条件で再戦")) RequestReplay();
        }
        if (ImGui::Button("デバッグを終了")) m_DebugRequest = 2;
        ImGui::End();
    }
    if (!m_DebugEditorOpen) return;
    GameUi::PrepareWindow("debug_mode_editor", ImVec2(50, 20), ImVec2(1120, 660));
    ImGui::Begin("デバッグモード：戦闘条件", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("編集中は戦闘を一時停止します。配置とデッキをまとめて適用し、HP・ターン・乱数を開始条件に戻して再開します。");
    ImGui::Checkbox("ステージエディターの配置を使用", &m_DebugUseStageLayout);
    ImGui::SameLine();
    ImGui::TextDisabled("OFF：敵編成・配置タブの手動条件／比較検証を使用");
    ImGui::BeginDisabled(IsRunActive() && (game.GetCurrentSceneType() != SceneType::Battle || game.IsClearRewardActive()));
    if (ImGui::Button("適用してステージを最初から開始", ImVec2(290, 36)))
    {
        if (m_DebugUseStageLayout || IsRunActive()) TestStageEditorLayout();
        else { m_DebugStageOverride.reset(); m_DebugRequest = 1; }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_DebugMode || m_DebugBattleFinished);
    if (ImGui::Button("編集中の条件を適用せず再開", ImVec2(260, 36))) { m_DebugEditorOpen = false; GameDebugHost::SetPaused(game, false); Game::ResetFrameTiming(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("タイトルへ戻る", ImVec2(180, 36))) m_DebugRequest = 2;
    if (ImGui::Button("デバッグ条件を保存")) SavePreset(); ImGui::SameLine();
    if (ImGui::Button("デバッグ条件を読み込む")) LoadPreset();
    ImGui::TextDisabled("条件プリセットはデッキと手動敵編成用です。配置のファイル保存はステージエディターで行います。");
    if (!m_DebugMessage.empty()) ImGui::TextWrapped("%s", m_DebugMessage.c_str());
    const auto error = m_DebugSetup.Validate();
    if (!error.empty()) ImGui::TextColored(ImVec4(1, .45f, .3f, 1), "%s", error.c_str());
    if (ImGui::BeginTabBar("debug_setup_tabs"))
    {
        const auto runTabFlags = m_DebugSelectRunTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        m_DebugSelectRunTab = false;
        if (ImGui::BeginTabItem("デバッグラン", nullptr, runTabFlags))
        {
            DrawRunTools(game);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ステージエディター"))
        {
            DrawStageEditor(game);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("デッキ"))
        {
            int catalogDrop = -1, deckDrop = -1, dropTarget = -1, removeIndex = -1;
            auto acceptDeckDrop = [&](int target)
            {
                // Include fast drags whose final move and release arrive in one frame.
                if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                {
                    catalogDrop = m_DebugDeckDragCatalog;
                    deckDrop = m_DebugDeckDragSlot;
                    dropTarget = target;
                }
                if (!ImGui::BeginDragDropTarget()) return;
                if (const auto* payload = ImGui::AcceptDragDropPayload("DEBUG_BALL_CATALOG"))
                {
                    catalogDrop = *static_cast<const int*>(payload->Data);
                    dropTarget = target;
                }
                if (const auto* payload = ImGui::AcceptDragDropPayload("DEBUG_DECK_SLOT"))
                {
                    deckDrop = *static_cast<const int*>(payload->Data);
                    dropTarget = target;
                }
                ImGui::EndDragDropTarget();
            };
            ImGui::Text("%d / 64個（登録%d種類・1個だけでも実験できます）",
                static_cast<int>(m_DebugSetup.deck.size()),
                static_cast<int>(m_DebugBallCatalog.size()));
            struct BallCategoryTab
            {
                const char* label;
                BallCategory category;
            };
            const BallCategoryTab categoryTabs[] = {
                { "標準", BallCategory::Standard },
                { "重量", BallCategory::Heavy },
                { "貫通", BallCategory::Pierce },
                { "反射", BallCategory::Bounce },
                { "アンカー", BallCategory::Anchor },
            };
            if (ImGui::BeginTabBar("debug_ball_category_tabs"))
            {
                for (const BallCategoryTab& tab : categoryTabs)
                {
                    if (!ImGui::BeginTabItem(tab.label)) continue;
                    bool firstButton = true;
                    for (const auto& definition : m_DebugBallCatalog)
                    {
                        if (definition.category != tab.category) continue;
                        if (!firstButton) ImGui::SameLine();
                        firstButton = false;
                        ImGui::PushID(definition.definitionId.c_str());
                        ImGui::BeginDisabled(m_DebugSetup.deck.size() >= 64);
                        if (ImGui::Button(BallLabel(definition.definitionId)))
                            m_DebugSetup.deck.push_back(definition);
                        if (ImGui::IsItemActivated())
                        {
                            m_DebugDeckDragCatalog = static_cast<int>(&definition - m_DebugBallCatalog.data());
                            m_DebugDeckDragSlot = -1;
                        }
                        if (ImGui::BeginDragDropSource())
                        {
                            const int index = static_cast<int>(&definition - m_DebugBallCatalog.data());
                            ImGui::SetDragDropPayload("DEBUG_BALL_CATALOG", &index, sizeof(index));
                            ImGui::Text("追加：%s", BallLabel(definition.definitionId));
                            ImGui::EndDragDropSource();
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::TextWrapped("球一覧から下へドラッグして追加。デッキの行をドラッグして並べ替え、削除エリアへドロップして削除できます。各行を開くと性能を編集できます。");
            ImGui::Button("ここへドロップして末尾に追加・移動", ImVec2(380, 32));
            acceptDeckDrop(static_cast<int>(m_DebugSetup.deck.size()));
            ImGui::SameLine();
            ImGui::Button("ここへドロップして削除", ImVec2(260, 32));
            if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                removeIndex = m_DebugDeckDragSlot;
            if (ImGui::BeginDragDropTarget())
            {
                if (const auto* payload = ImGui::AcceptDragDropPayload("DEBUG_DECK_SLOT"))
                    removeIndex = *static_cast<const int*>(payload->Data);
                ImGui::EndDragDropTarget();
            }
            ImGui::BeginChild("debug_deck_entries", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (size_t i = 0; i < m_DebugSetup.deck.size();)
            {
                auto& ball = m_DebugSetup.deck[i]; ImGui::PushID(static_cast<int>(i));
                bool remove = ImGui::SmallButton("削除"); ImGui::SameLine();
				const bool open = ImGui::TreeNode("ball", "#%d %s +%d  攻撃%d", static_cast<int>(i + 1), BallLabel(ball.definitionId), ball.upgradeLevel, ball.status.attack);
                if (ImGui::IsItemActivated())
                {
                    m_DebugDeckDragSlot = static_cast<int>(i);
                    m_DebugDeckDragCatalog = -1;
                }
                if (ImGui::BeginDragDropSource())
                {
                    const int index = static_cast<int>(i);
                    ImGui::SetDragDropPayload("DEBUG_DECK_SLOT", &index, sizeof(index));
                    ImGui::Text("#%d %s", index + 1, BallLabel(ball.definitionId));
                    ImGui::EndDragDropSource();
                }
                acceptDeckDrop(static_cast<int>(i));
                if (open)
                {
                    int level = ball.upgradeLevel;
                    if (ImGui::SliderInt("強化段階（既定性能に戻す）", &level, 0, 2))
                    {
                        const auto found = std::find_if(m_DebugBallCatalog.begin(), m_DebugBallCatalog.end(), [&](const auto& b) { return b.definitionId == ball.definitionId; });
                        if (found != m_DebugBallCatalog.end()) { ball = *found; ball.upgradeLevel = level; if (level > 0) ball.status = ball.upgradeTable[level - 1]; }
                    }
					if (ImGui::TreeNode("性能を個別に変更")) { EditStatus(ball.status, false); ImGui::TreePop(); }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) removeIndex = static_cast<int>(i);
                ++i;
            }
            ImGui::EndChild();
            // Apply after drawing: vector mutations would invalidate the current row reference.
            auto& deck = m_DebugSetup.deck;
            if (removeIndex >= 0 && removeIndex < static_cast<int>(deck.size()))
                deck.erase(deck.begin() + removeIndex);
            else if (dropTarget >= 0 && dropTarget <= static_cast<int>(deck.size()))
            {
                if (catalogDrop >= 0 && catalogDrop < static_cast<int>(m_DebugBallCatalog.size()) && deck.size() < 64)
                    deck.insert(deck.begin() + dropTarget, m_DebugBallCatalog[catalogDrop]);
                else if (deckDrop >= 0 && deckDrop < static_cast<int>(deck.size()))
                {
                    auto moved = deck[deckDrop];
                    deck.erase(deck.begin() + deckDrop);
                    if (deckDrop < dropTarget) --dropTarget;
                    deck.insert(deck.begin() + dropTarget, std::move(moved));
                }
            }
            if (ImGui::IsMouseReleased(0)) m_DebugDeckDragCatalog = m_DebugDeckDragSlot = -1;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HP・所持金・レリック"))
        {
            ImGui::InputInt("最大HP", &m_DebugSetup.maxHp);
            m_DebugSetup.maxHp = std::clamp(m_DebugSetup.maxHp, 1, 9999);
            m_DebugSetup.hp = (std::min)(m_DebugSetup.hp, m_DebugSetup.maxHp);
            ImGui::InputInt("開始HP", &m_DebugSetup.hp);
            m_DebugSetup.hp = std::clamp(m_DebugSetup.hp, 1, m_DebugSetup.maxHp);
            ImGui::InputInt("所持金", &m_DebugSetup.money, 10, 100);
            m_DebugSetup.money = std::clamp(m_DebugSetup.money, 0, 999999);
            const std::uint32_t seedStep = 1;
            ImGui::InputScalar("乱数シード", ImGuiDataType_U32, &m_DebugSetup.seed, &seedStep);
            ImGui::TextDisabled("同じ条件・シードでデッキの抽選を再現します。基準難易度とDDAは無効、選択したアセンションだけを適用します。");
            for (int i = 0; i < game.GetRelicCount(); ++i)
            {
                const auto* relic = game.GetRelic(i);
                ImGui::Checkbox(relic->name, &m_DebugSetup.relics[static_cast<size_t>(i)]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", relic->description);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("進行・アセンション"))
        {
            ImGui::TextWrapped(
                "ここでの解除状態はデバッグ戦とMCP表示だけに適用され、通常プロフィールへは保存されません。");
            if (ImGui::Button("現在の通常プロフィールをコピー"))
            {
                const ProgressionProfile& profile = m_DebugPreviousRun.has_value()
                    ? m_DebugPreviousRun->progressionProfile
                    : game.GetProgressionProfile();
                m_DebugSetup.highestUnlockedAscension =
                    profile.highestUnlockedAscension;
                m_DebugSetup.selectedAscension = profile.selectedAscension;
                m_DebugSetup.achievements = profile.achievements;
            }
            ImGui::SliderInt(
                "解放済み最高アセンション",
                &m_DebugSetup.highestUnlockedAscension,
                0,
                ProgressionProfile::MaximumAscension,
                "A%d",
                ImGuiSliderFlags_AlwaysClamp);
            m_DebugSetup.selectedAscension = std::clamp(
                m_DebugSetup.selectedAscension,
                0,
                m_DebugSetup.highestUnlockedAscension);
            ImGui::SliderInt(
                "今回適用するアセンション",
                &m_DebugSetup.selectedAscension,
                0,
                m_DebugSetup.highestUnlockedAscension,
                "A%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::Text(
                "通常ランの開始HP %d/%d / 敵HP x%.2f / 敵攻撃 +%d / 休憩回復 %.0f%%",
                m_DebugSetup.DefaultStartingHp(), m_DebugSetup.maxHp,
                ProgressionProfile::EnemyHpMultiplier(m_DebugSetup.selectedAscension),
                ProgressionProfile::EnemyAttackBonus(m_DebugSetup.selectedAscension),
                GameDebugHost::GetDefaultRestHealRatio(game) * 100.0f);
            if (m_DebugSetup.selectedAscension == 0)
                ImGui::BulletText("A0  %s", ProgressionProfile::AscensionRule(0));
            else
                for (int level = 1; level <= m_DebugSetup.selectedAscension; ++level)
                    ImGui::BulletText(
                        "A%d  %s",
                        level,
                        ProgressionProfile::AscensionRule(level));

            ImGui::SeparatorText("実績の解除状態");
            if (ImGui::Button("すべて解除"))
                m_DebugSetup.achievements.fill(true);
            ImGui::SameLine();
            if (ImGui::Button("すべて未解除"))
                m_DebugSetup.achievements.fill(false);
            ImGui::BeginChild(
                "debug_achievement_list",
                ImVec2(0.0f, 250.0f),
                ImGuiChildFlags_Borders);
            for (const AchievementDefinition& achievement : AchievementCatalog)
            {
                const size_t index = static_cast<size_t>(achievement.id);
                ImGui::PushID(static_cast<int>(index));
                ImGui::Checkbox(
                    achievement.name,
                    &m_DebugSetup.achievements[index]);
                ImGui::TextDisabled("解放：%s", achievement.reward);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("条件：%s", achievement.condition);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("敵編成・配置"))
        {
            ImGui::SeparatorText("HP・敵数・配置の比較検証");
            ImGui::Checkbox(
                "比較検証設定を有効化",
                &m_DebugSetup.enemyValidation.enabled);
            ImGui::TextDisabled(
                "有効時は次の戦闘だけ、既存3種で低/標準/高耐久の固定比率を生成します。");
            const char* hpLabels[] = { "x1.0", "x1.5", "x2.0" };
            const float hpValues[] = { 1.0f, 1.5f, 2.0f };
            int hpIndex = m_DebugSetup.enemyValidation.hpMultiplier < 1.25f
                ? 0
                : (m_DebugSetup.enemyValidation.hpMultiplier < 1.75f ? 1 : 2);
            if (ImGui::Combo("敵HP倍率", &hpIndex, hpLabels, IM_ARRAYSIZE(hpLabels)))
                m_DebugSetup.enemyValidation.hpMultiplier = hpValues[hpIndex];
            const char* countLabels[] = { "6体", "8体", "10体", "12体" };
            const int countValues[] = { 6, 8, 10, 12 };
            int countIndex = m_DebugSetup.enemyValidation.enemyCount == 6
                ? 0
                : (m_DebugSetup.enemyValidation.enemyCount == 10
					? 2
					: (m_DebugSetup.enemyValidation.enemyCount == 12 ? 3 : 1));
            if (ImGui::Combo("敵数", &countIndex, countLabels, IM_ARRAYSIZE(countLabels)))
                m_DebugSetup.enemyValidation.enemyCount = countValues[countIndex];
            const char* patternLabels[] = { "Dense（密集）", "Line（直線）", "Random（シード配置）" };
            int patternIndex = static_cast<int>(
                m_DebugSetup.enemyValidation.layoutPattern);
            if (ImGui::Combo(
                "配置パターン",
                &patternIndex,
                patternLabels,
                IM_ARRAYSIZE(patternLabels)))
            {
                m_DebugSetup.enemyValidation.layoutPattern =
                    static_cast<EnemyValidationLayoutPattern>(patternIndex);
            }
            ImGui::TextDisabled(
                "6体=2/3/1、8体=3/4/1、10体=4/5/1、12体=5/6/1（%s / %s / %s）。",
                EnemyLabel("enemy_normal"), EnemyLabel("enemy_strong"), EnemyLabel("enemy_tank"));
            ImGui::TextDisabled(
                "Random配置は「HP・所持金・レリック」の乱数シードで再現します。");
            if (m_DebugSetup.enemyValidation.enabled)
            {
                ImGui::TextColored(
                    ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                    "下の手動敵編成・位置・開始HPは比較検証中は使用しません。");
            }
            ImGui::SeparatorText("手動の敵編成・配置");
            if (ImGui::BeginCombo("既存ステージの初期配置を読み込む", "ステージを選択"))
            {
                for (const auto& stage : m_DebugStages)
                {
                    const auto label = stage.DisplayName() + "###" + stage.id;
                    if (!ImGui::Selectable(label.c_str())) continue;
                    m_DebugSetup.enemies.clear();
                    for (const auto& spawn : stage.enemies) m_DebugSetup.enemies.push_back({spawn, spawn.enemyData.maxHp});
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("敵を追加", "種類を選択"))
            {
                for (const auto& definition : m_DebugEnemyCatalog)
                    if (ImGui::Selectable(EnemyLabel(definition.id)) && m_DebugSetup.enemies.size() < 32)
                    {
                        EnemySpawnData spawn; spawn.enemyId = definition.id; spawn.enemyData = definition;
                        spawn.position = {0, TableConfig::FIELD_HEIGHT, 20};
                        m_DebugSetup.enemies.push_back({spawn, definition.maxHp});
                    }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Xが左右、Zが前後です。球同士の重なりも、不具合の再現用に指定できます。");
            ImGui::SliderFloat("自球 X", &m_DebugSetup.playerPosition.x, -65, 65);
            ImGui::SliderFloat("自球 Z", &m_DebugSetup.playerPosition.z, -33, 33);
            ImGui::SliderInt("ボス Break残りショット", &m_DebugSetup.breakShots, 0, 2);
            if (m_DebugSetup.breakShots > 0) m_DebugSetup.armor = 0;
            else { m_DebugSetup.armor = (std::max)(1, m_DebugSetup.armor); ImGui::SliderInt("ボス Armor", &m_DebugSetup.armor, 1, 2); }
            ImGui::TextWrapped("ボスの取り巻きは本編ルールで自動生成されます（旧ブレイクボール配置は不使用）。");
            for (size_t i = 0; i < m_DebugSetup.enemies.size();)
            {
                auto& enemy = m_DebugSetup.enemies[i]; ImGui::PushID(static_cast<int>(i));
                bool remove = ImGui::SmallButton("削除"); ImGui::SameLine();
                if (ImGui::TreeNode("enemy", "#%d %s HP%d/%d (%.1f, %.1f)", static_cast<int>(i + 1), EnemyLabel(enemy.spawn.enemyData.id), enemy.hp, enemy.spawn.enemyData.maxHp, enemy.spawn.position.x, enemy.spawn.position.z))
                {
                    ImGui::SliderFloat("X", &enemy.spawn.position.x, -65, 65);
                    ImGui::SliderFloat("Z", &enemy.spawn.position.z, -33, 33);
                    ImGui::SliderInt("最大HP", &enemy.spawn.enemyData.maxHp, 1, 9999, "%d", ImGuiSliderFlags_AlwaysClamp);
                    enemy.hp = (std::min)(enemy.hp, enemy.spawn.enemyData.maxHp);
                    ImGui::SliderInt("現在HP", &enemy.hp, 1, enemy.spawn.enemyData.maxHp, "%d", ImGuiSliderFlags_AlwaysClamp);
					if (ImGui::TreeNode("性能")) { EditStatus(enemy.spawn.enemyData.status, true); ImGui::TreePop(); }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) m_DebugSetup.enemies.erase(m_DebugSetup.enemies.begin() + i); else ++i;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Gameは呼び出し窓口だけを保ち、デバッグの手順はControllerへ委譲
/// <summary>
/// デバッグコントローラーにエディターを開くよう依頼する。
/// </summary>
void Game::OpenDebugMode() { m_DebugController->Open(*this); }
/// <summary>
/// デバッグモードが有効かを返す。
/// </summary>
bool Game::IsDebugMode() const { return m_DebugController->IsActive(); }
/// <summary>
/// デバッグモードでランが進行中かを返す。
/// </summary>
bool Game::IsDebugRunMode() const { return m_DebugController->IsRunActive(); }
/// <summary>
/// デバッグ戦闘のブレイクで使用する球の配置を取得する。
/// </summary>
const std::vector<DirectX::SimpleMath::Vector3>& Game::GetDebugBreakBallPositions() const
{
    return m_DebugController->GetActiveBreakBallPositions();
}
/// <summary>
/// プレイヤー球にデバッグ戦闘の設定を適用する。
/// </summary>
void Game::ApplyDebugBattlePlayer(PlayerBall* player) { m_DebugController->ApplyBattlePlayer(player); }
/// <summary>
/// 指定位置の敵球にデバッグ戦闘の設定を適用する。
/// </summary>
void Game::ApplyDebugBattleEnemy(EnemyBall* enemy, std::size_t index) { m_DebugController->ApplyBattleEnemy(enemy, index); }
/// <summary>
/// 現在のデバッグ設定をランへ適用する。
/// </summary>
void Game::ApplyDebugRunSettings() { m_DebugController->ApplyRunSettings(*this); }
/// <summary>
/// デバッグセッションを終了する。
/// </summary>
void Game::EndDebugMode() { m_DebugController->End(*this); }
/// <summary>
/// 勝敗を指定してデバッグ戦闘を終了する。
/// </summary>
void Game::FinishDebugBattle(bool victory) { m_DebugController->FinishBattle(*this, victory); }
/// <summary>
/// デバッグモードの入力と状態を更新し、通常のゲーム進行を止めるかを返す。
/// </summary>
bool Game::UpdateDebugMode() { return m_DebugController->Update(*this); }
/// <summary>
/// デバッグモードの操作画面を描画する。
/// </summary>
void Game::DrawDebugMode() { m_DebugController->Draw(*this); }
/// <summary>
/// 保存済みのデバッグ戦闘プリセットを読み込む。
/// </summary>
bool Game::LoadDebugPreset() { return m_DebugController->LoadPreset(); }
